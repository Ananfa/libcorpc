/*
 * Created by Xianke Liu on 2021/6/18.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "corpc_routine_env.h"
#include "corpc_message_client.h"
#include "corpc_kcp.h"
#include "corpc_crc.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

using namespace corpc;

#ifdef NEW_MESSAGE_CLIENT_IMPLEMENT

MessageClient::MessageClient(IO *io, Worker *worker, MessageTerminal *terminal): io_(io), terminal_(terminal) {
    if (worker) {
        worker_ = worker;
    } else {
        worker_ = io->getWorker();
    }
}

MessageClient::~MessageClient() {
    if (connection_) {
        connection_->close();

        connection_.reset();
    }
}

void MessageClient::send(int32_t type, bool isRaw, bool needCrypt, bool needBuffer, uint16_t tag, std::shared_ptr<void> msg) {
    if (connection_) {
        connection_->send(type, isRaw, needCrypt, needBuffer, tag, msg);
    }
}

std::shared_ptr<corpc::Connection> MessageClient::buildAndAddConnection(int fd) {
    LOG("fd %d connected\n", fd);
    std::shared_ptr<corpc::Connection> connection(buildConnection(fd));
    std::shared_ptr<corpc::Pipeline> pipeline = pipelineFactory_->buildPipeline(connection);
    connection->setPipeline(pipeline);

    // 判断是否需要心跳
    if (connection->needHB()) {
        Heartbeater::Instance().addConnection(connection);
    }
    
    // 注意：onConnect原先是放在最后处理，现在调整到这里。原因是发现放在最后会出现连接消息处理前就收到业务消息处理，
    //      会出现“onConnect中会有conn->close()操作导致连接未加到IO就先要从IO删除的问题”，因此需要上锁
    // 通知连接建立
    {
        LockGuard lock(connection->getLock());

        connection->onConnect();
        
        // 将接受的连接分别发给Receiver和Sender
        io_->addConnection(connection);
    }

    return connection;
}

corpc::Connection *MessageClient::buildConnection(int fd) {
    return terminal_->buildConnection(fd, io_, worker_);
}

TcpClient::TcpClient(IO *io, Worker *worker, MessageTerminal *terminal, const std::string& host, uint16_t port): MessageClient(io, worker, terminal), host_(host), port_(port) {
    pipelineFactory_.reset(new TcpPipelineFactory(worker_, MessageTerminal::decode, MessageTerminal::encode, CORPC_MESSAGE_HEAD_SIZE, CORPC_MAX_MESSAGE_SIZE, 0, MessagePipeline::FOUR_BYTES));
}

bool TcpClient::connect() {
    if (connection_ && connection_->isOpen()) {
        return false;
    }

    int s;
    if ((s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        ERROR_LOG("can't create socket\n");
        return false;
    }
    co_set_timeout(s, -1, 1000);
    
    struct sockaddr_in addr;
    bzero(&addr,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    int nIP = inet_addr(host_.c_str());
    addr.sin_addr.s_addr = nIP;
    
    int ret = ::connect(s, (struct sockaddr*)&addr, sizeof(addr));
    if (ret == -1) {
        if ( errno == EALREADY || errno == EINPROGRESS ) {
            struct pollfd pf = { 0 };
            pf.fd = s;
            pf.events = (POLLOUT|POLLERR|POLLHUP);
            co_poll( co_get_epoll_ct(),&pf,1,200);
            //check connect
            int error = 0;
            uint32_t socklen = sizeof(error);
            errno = 0;
            ret = getsockopt(s, SOL_SOCKET, SO_ERROR,(void *)&error,  &socklen);
            if ( ret == -1 ) {
                // 出错处理
                ERROR_LOG("TcpClient::connect getsockopt fd %d ret %d errno %d (%s)\n",
                       s, ret, errno, strerror(errno));
                
                ::close(s);
                return false;
            } else if ( error ) {
                // 出错处理
                ERROR_LOG("TcpClient::connect getsockopt fd %d ret %d errno %d (%s)\n",
                       s, ret, error, strerror(error));
                
                ::close(s);
                return false;
            }
        } else {
            // 出错处理
            ERROR_LOG("TcpClient::connect connect fd %d ret %d errno %d (%s)\n",
                   s, ret, errno, strerror(errno));
            
            ::close(s);
            return false;
        }
    }
    
    setKeepAlive(s, 10);

    std::shared_ptr<MessageTerminal::Connection> con = std::static_pointer_cast<MessageTerminal::Connection>(buildAndAddConnection(s));
    if (connection_) {
        con->setRecvSerial(connection_->getRecvSerial());
    }

    connection_ = con;

    return true;
}

UdpClient::UdpClient(IO *io, Worker *worker, MessageTerminal *terminal, const std::string& host, uint16_t port, uint16_t local_port): MessageClient(io, worker, terminal), host_(host), port_(port), local_port_(local_port) {
    pipelineFactory_.reset(new UdpPipelineFactory(worker_, MessageTerminal::decode, MessageTerminal::encode, CORPC_MESSAGE_HEAD_SIZE, CORPC_MAX_UDP_MESSAGE_SIZE));
}

bool UdpClient::connect() {
    struct sockaddr_in si_me, si_other;
    socklen_t slen = sizeof(si_other);
    char buf[CORPC_MAX_UDP_MESSAGE_SIZE]; // TODO: 在堆上建立缓存
    
    if (connection_ && connection_->isOpen()) {
        return false;
    }

    int s;
    if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        ERROR_LOG("can't create socket\n");
        return false;
    }
    
    // zero out the structure
    memset((char *)&si_me, 0, sizeof(si_me));
    
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(local_port_);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int reuse = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse,sizeof(reuse));
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
    
    //bind socket to port
    if(bind(s, (struct sockaddr*)&si_me, sizeof(si_me)) == -1) {
        ERROR_LOG("can't bind socket\n");
        ::close(s);
        return false;
    }
    
    memset((char *) &si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(port_);
    int nIP = inet_addr(host_.c_str());
    si_other.sin_addr.s_addr = nIP;
    
    if(::connect(s, (struct sockaddr *)&si_other, slen) == -1) {
        ERROR_LOG("can't connect\n");
        ::close(s);
        return false;
    }
    
    struct pollfd fd;
    int ret;
    
    fd.fd = s;
    fd.events = POLLIN;
    
    // 准备握手消息
    char handshake1msg[CORPC_MESSAGE_HEAD_SIZE];
    char handshake3msg[CORPC_MESSAGE_HEAD_SIZE];
    
    memset(handshake1msg, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int32_t *)(handshake1msg + 4) = htobe32(CORPC_MSG_TYPE_UDP_HANDSHAKE_1);
    
    memset(handshake3msg, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int32_t *)(handshake3msg + 4) = htobe32(CORPC_MSG_TYPE_UDP_HANDSHAKE_3);
    
    // 握手阶段一：发送handshake1消息，然后等待handshake2消息到来，超时未到则重发handshake1消息
    while (true) {
        // 阶段一：发送handshake1消息
        if (write(s, handshake1msg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
            ERROR_LOG("can't send handshake1\n");
            ::close(s);
            return false;
        }
        DEBUG_LOG("send handshake1\n");

        // 阶段二：接收handshake2消息
        uint64_t nowms = mtime();
        uint64_t pollEndAt = nowms + 1000;
STEP1_POLLAGAIN:
        ret = poll(&fd, 1, pollEndAt - nowms); // 1 second for timeout
        switch (ret) {
            case -1:
                if (errno == EAGAIN || errno == EINTR) {
                    nowms = mtime();
                    if (nowms < pollEndAt) {
                        goto STEP1_POLLAGAIN;
                    }

                    continue;
                }

                ERROR_LOG("can't recv handshake2\n");
                ::close(s);
                return false;
            case 0:
                continue; // 回阶段一
            default: {
                ret = (int)read(s, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
                if (ret != CORPC_MESSAGE_HEAD_SIZE) {
                    ERROR_LOG("recv data size error\n");
                    ::close(s);
                    return false;
                }
                
                int32_t msgtype = *(int32_t *)(buf + 4);
                msgtype = be32toh(msgtype);
                
                if (msgtype != CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                    ERROR_LOG("recv data not handshake2\n");
                    ::close(s);
                    return false;
                }
                DEBUG_LOG("recv handshake2\n");
                goto STEP2;
            }
        }
    }

STEP2:
    while (true) {
        // 阶段三：发hankshake3消息
        if (write(s, handshake3msg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
            ERROR_LOG("can't send handshake3\n");
            ::close(s);
            return false;
        }
        DEBUG_LOG("send handshake3\n");

        // 阶段四：接收handshake4消息
        uint64_t nowms = mtime();
        uint64_t pollEndAt = nowms + 100;
STEP2_POLLAGAIN:
        ret = poll(&fd, 1, pollEndAt - nowms); // 100 millisecond for timeout
        switch (ret) {
            case -1:
                if (errno == EAGAIN || errno == EINTR) {
                    nowms = mtime();
                    if (nowms < pollEndAt) {
                        goto STEP2_POLLAGAIN;
                    }

                    continue;
                }

                ERROR_LOG("can't recv handshake4\n");
                ::close(s);
                return false;
            case 0:
                continue; // 回阶段三
            default: {
                ret = (int)read(s, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
                if (ret != CORPC_MESSAGE_HEAD_SIZE) {
                    ERROR_LOG("recv data size error\n");
                    ::close(s);
                    return false;
                }
                
                int32_t msgtype = *(int32_t *)(buf + 4);
                msgtype = be32toh(msgtype);
                
                if (msgtype == CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                    continue;
                }

                if (msgtype != CORPC_MSG_TYPE_UDP_HANDSHAKE_4) {
                    ERROR_LOG("recv data not handshake4\n");
                    ::close(s);
                    return false;
                }
                DEBUG_LOG("recv handshake4\n");
                goto STEP3;
            }
        }
    }

STEP3:
    std::shared_ptr<MessageTerminal::Connection> con = std::static_pointer_cast<MessageTerminal::Connection>(buildAndAddConnection(s));
    if (connection_) {
        con->setRecvSerial(connection_->getRecvSerial());
    }

    connection_ = con;

    return true;
}

KcpClient::KcpClient(IO *io, Worker *worker, KcpMessageTerminal *terminal, const std::string& host, uint16_t port, uint16_t local_port): UdpClient(io, worker, terminal, host, port, local_port) {
    pipelineFactory_.reset(new KcpPipelineFactory(worker_, MessageTerminal::decode, MessageTerminal::encode, CORPC_MESSAGE_HEAD_SIZE, CORPC_MAX_UDP_MESSAGE_SIZE, 0, MessagePipeline::FOUR_BYTES));
}

#else

//uint8_t MessageClient::_heartbeatmsg[CORPC_MESSAGE_HEAD_SIZE];

MessageClient::~MessageClient() {
    MessageInfo *info = sendQueue_.pop();
    while (info) {
        WARN_LOG("MessageClient::~MessageClient() something in send queue need to delete\n");
        delete info;
        info = sendQueue_.pop();
    }

    info = recvQueue_.pop();
    while (info) {
        WARN_LOG("MessageClient::~MessageClient() something in recv queue need to delete\n");
        delete info;
        info = recvQueue_.pop();
    }
}

void MessageClient::close() {
    //if (running_) {
    //    running_ = false;
        LOG("MessageClient::close fd: %d\n", s_);
        ::close(s_);
    //}
}

void MessageClient::send(int32_t type, uint16_t tag, bool needCrypter, std::shared_ptr<google::protobuf::Message> msg) {
    MessageInfo *info = new MessageInfo;
    info->type = type;
    info->tag = tag;
    info->proto = msg;
    info->needCrypter = needCrypter;
    
    sendQueue_.push(info);
}

void MessageClient::recv(int32_t& type, uint16_t& tag, std::shared_ptr<google::protobuf::Message>& msg) {
    MessageInfo *info = recvQueue_.pop();
    if (info) {
        type = info->type;
        tag = info->tag;
        msg = info->proto;
        
        delete info;
    } else {
        type = 0;
        tag = 0;
        msg = nullptr;
    }
}

bool MessageClient::registerMessage(int32_t type, std::shared_ptr<google::protobuf::Message> proto) {
    MessageInfo info;
    info.type = type;
    info.proto = proto;
    
    registerMessageMap_.insert(std::make_pair(type, info));
    
    return false;
}

bool TcpClient::start() {
    if ((s_=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        ERROR_LOG("can't create socket\n");
        return false;
    }
    
    struct sockaddr_in addr;
    bzero(&addr,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    int nIP = inet_addr(host_.c_str());
    addr.sin_addr.s_addr = nIP;
    
    if (connect(s_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        ERROR_LOG("can't connect\n");
        ::close(s_);
        return false;
    }
    
    if (needHB_) {
        struct timeval now = { 0 };
        gettimeofday( &now,NULL );
        lastRecvHBTime_ = now.tv_sec;
        lastRecvHBTime_ *= 1000;
        lastRecvHBTime_ += now.tv_usec / 1000;
        
        lastSendHBTime_ = lastRecvHBTime_;
    }

    running_ = true;
    
    // 启动数据收发协程
    RoutineEnvironment::startCoroutine(workRoutine, this);
    
    return true;
}

void *TcpClient::workRoutine( void * arg ) {
    std::shared_ptr<TcpClient> self = std::static_pointer_cast<TcpClient>(((TcpClient*)arg)->getPtr());

    std::string buffs(CORPC_MESSAGE_HEAD_SIZE + CORPC_MAX_MESSAGE_SIZE,0);
    uint8_t *buf = (uint8_t *)buffs.data();

    uint8_t heartbeatmsg[CORPC_MESSAGE_HEAD_SIZE];
    memset(heartbeatmsg, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int32_t *)(heartbeatmsg + 4) = htobe32(CORPC_MSG_TYPE_HEARTBEAT);
    
    int s = self->s_;
    struct pollfd fd_in, fd_out;
    fd_out.fd = fd_in.fd = s;
    fd_in.events = POLLIN;
    fd_out.events = POLLOUT;
    
    int ret;
    
    std::string head(CORPC_MESSAGE_HEAD_SIZE,0);
    uint8_t *headBuf = (uint8_t *)head.data();
    
    std::string body(CORPC_MAX_MESSAGE_SIZE,0);
    uint8_t *bodyBuf = (uint8_t *)body.data();
    
    int headNum = 0;
    int bodyNum = 0;
    
    uint32_t bodySize = 0;
    int32_t msgType = 0;
    uint16_t tag = 0;
    uint16_t flag = 0;
    
    uint64_t nowms = 0;
    // 开始定时心跳，以及接收／发送数据包
    // 逻辑：采用轮询机制
    //   1.每4ms尝试一次数据读取
    //   2.若数据是心跳，记录接收心跳的最新时间
    //   3.若超时未收到心跳，表示网络连接故障，进行断线处理
    //   4.若到心跳时间，发心跳
    //   5.查看是否有数据需要发送，有则发送
    //   6.回第1步
    while (self->running_) {
        ret = poll(&fd_in, 1, 4);

        if (self->needHB_) {
            struct timeval now = { 0 };
            gettimeofday( &now,NULL );
            nowms = now.tv_sec;
            nowms *= 1000;
            nowms += now.tv_usec / 1000;
        }
        
        // BUG: 当连接在其他地方close时，poll会返回0，死循环。而且还会在文件描述符被重新打开时错误读取信息。
        if (ret) {
            if (ret < 0) {
                if (errno == EAGAIN || errno == EINTR) {
                    continue;
                }

                DEBUG_LOG("poll fd_in\n");
                self->close();
                return nullptr;
            }

            // 一次性读取尽可能多的数据
            ret = (int)read(s, buf, CORPC_MESSAGE_HEAD_SIZE + CORPC_MAX_MESSAGE_SIZE);
            if (ret <= 0) {
                // ret 0 mean disconnected
                if (ret < 0 && (errno == EAGAIN || errno == EINTR)) {
                    continue;
                }
                
                DEBUG_LOG("read data error, fd: %d\n");
                self->close();
                return nullptr;
            }
            
            int remainNum = ret;
            int readNum = 0;
            
            while (remainNum) {
                // 解析消息头部
                if (headNum < CORPC_MESSAGE_HEAD_SIZE) {
                    if (remainNum <= CORPC_MESSAGE_HEAD_SIZE - headNum) {
                        memcpy(headBuf + headNum, buf + readNum, remainNum);
                        readNum += remainNum;
                        headNum += remainNum;
                        remainNum = 0;
                    } else {
                        memcpy(headBuf + headNum, buf + readNum, CORPC_MESSAGE_HEAD_SIZE - headNum);
                        readNum += CORPC_MESSAGE_HEAD_SIZE - headNum;
                        remainNum -= CORPC_MESSAGE_HEAD_SIZE - headNum;
                        headNum = CORPC_MESSAGE_HEAD_SIZE;
                    }
                    
                    if (headNum == CORPC_MESSAGE_HEAD_SIZE) {
                        bodySize = *(uint32_t *)headBuf;
                        bodySize = be32toh(bodySize);
                        msgType = *(int32_t *)(headBuf + 4);
                        msgType = be32toh(msgType);
                        tag = *(uint16_t *)(headBuf + 8);
                        tag = be16toh(tag);
                        flag = *(uint16_t *)(headBuf + 10);
                        flag = be16toh(flag);
                    } else {
                        assert(remainNum == 0);
                        break;
                    }
                }

                // 解析消息体
                if (remainNum > 0) {
                    if (bodyNum < bodySize) {
                        if (remainNum <= bodySize - bodyNum) {
                            memcpy(bodyBuf + bodyNum, buf + readNum, remainNum);
                            readNum += remainNum;
                            bodyNum += remainNum;
                            remainNum = 0;
                        } else {
                            memcpy(bodyBuf + bodyNum, buf + readNum, bodySize - bodyNum);
                            readNum += bodySize - bodyNum;
                            remainNum -= bodySize - bodyNum;
                            bodyNum = bodySize;
                        }
                    }
                }
                
                if (bodyNum == bodySize) {
                    // 根据消息类型解析和处理消息
                    if (msgType == CORPC_MSG_TYPE_HEARTBEAT) {
                        assert(bodySize == 0);
                        self->lastRecvHBTime_ = nowms;
                    } else if (msgType < 0) {
                        // 其他连接控制消息，不做处理
                    } else {
                        //assert(bodySize > 0);
                        // 校验序列号
                        if (self->enableSerial_) {
                            uint32_t serial = *(uint32_t *)(headBuf + 16);
                            serial = be32toh(serial);

                            if (serial != 0) {
                                if (serial != self->lastRecvSerial_ + 1) {
                                    ERROR_LOG("serial check failed, need:%d, get:%d\n", self->lastRecvSerial_+1, serial);
                                    self->close();
                                    return nullptr;
                                }

                                self->lastRecvSerial_++;
                            }
                        }

                        if (bodySize > 0) {
                            // 校验CRC
                            if (self->enableRecvCRC_) {
                                uint16_t crc = *(uint16_t *)(headBuf + 20);
                                crc = be16toh(crc);

                                uint16_t crc1 = corpc::CRC::CheckSum(bodyBuf, 0xFFFF, bodySize);

                                if (crc != crc1) {
                                    ERROR_LOG("crc check failed, msgType:%d, size:%d, recv:%d, cal:%d\n", msgType, bodySize, crc, crc1);
                                    self->close();
                                    return nullptr;
                                }
                            }

                            // 解密
                            if (flag & CORPC_MESSAGE_FLAG_CRYPT) {
                                if (self->crypter_ == nullptr) {
                                    ERROR_LOG("cant decrypt message for crypter not exist\n");
                                    self->close();
                                    return nullptr;
                                }

                                self->crypter_->decrypt(bodyBuf, bodyBuf, bodySize);
                            }
                        }
                        
                        
                        // 解码数据
                        auto iter = self->registerMessageMap_.find(msgType);
                        if (iter == self->registerMessageMap_.end()) {
                            ERROR_LOG("unknown message: %d\n", msgType);
                            self->close();
                            return nullptr;
                        }
                        
                        std::shared_ptr<google::protobuf::Message> msg = nullptr;
                        if (bodySize > 0) {
                            msg.reset(iter->second.proto->New());
                            if (!msg->ParseFromArray(bodyBuf, bodySize)) {
                                // 出错处理
                                ERROR_LOG("parse body fail for message: %d\n", msgType);
                                self->close();
                                return nullptr;
                            }
                        }
                        
                        MessageInfo *info = new MessageInfo;
                        info->type = msgType;
                        info->tag = tag;
                        info->proto = msg;
                        
                        self->recvQueue_.push(info);
                    }
                    
                    // 处理完一个消息，需要清理变量
                    headNum = 0;
                    bodyNum = 0;
                    
                    msgType = 0;
                    bodySize = 0;
                }
            }
        }
        
        // 心跳判断
        if (self->needHB_) {
            if (nowms - self->lastRecvHBTime_ > CORPC_MAX_NO_HEARTBEAT_TIME) {
                // 无心跳，断线
                ERROR_LOG("heartbeat timeout\n");
                self->close();
                return nullptr;
            }
            
            if (nowms - self->lastSendHBTime_ > CORPC_HEARTBEAT_PERIOD) {
                if (self->enableSerial_) {
                    *(uint32_t *)(heartbeatmsg + 12) = htobe32(self->lastRecvSerial_);
                }

                if (write(s, heartbeatmsg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
                    ERROR_LOG("write heartbeat\n");
                    self->close();
                    return nullptr;
                }
                
                self->lastSendHBTime_ = nowms;
            }
        }

        // 将要发送的数据拼在一起发送，提高效率
        int sendNum = 0;
        // 发送数据
        MessageInfo *info = self->sendQueue_.pop();
        while (info) {
            uint32_t msgSize = info->proto->GetCachedSize();
            if (msgSize == 0) {
                msgSize = info->proto->ByteSizeLong();
            }
            
            if (msgSize + CORPC_MESSAGE_HEAD_SIZE > CORPC_MAX_MESSAGE_SIZE) {
                ERROR_LOG("message size too large\n");
                self->close();
                return nullptr;
            }
            
            if (msgSize + CORPC_MESSAGE_HEAD_SIZE <= CORPC_MAX_MESSAGE_SIZE - sendNum) {
                info->proto->SerializeWithCachedSizesToArray(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE);

                if (info->needCrypter) {
                    assert(self->crypter_);
                    self->crypter_->encrypt(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, msgSize);
                    *(uint16_t *)(buf + sendNum + 10) = htobe16(CORPC_MESSAGE_FLAG_CRYPT);
                } else {
                    *(uint16_t *)(buf + sendNum + 10) = 0;
                }
                
                *(uint32_t *)(buf + sendNum) = htobe32(msgSize);
                *(int32_t *)(buf + sendNum + 4) = htobe32(info->type);

                *(uint16_t *)(buf + sendNum + 8) = htobe16(info->tag);
                
                if (self->enableSerial_) {
                    *(uint32_t *)(buf + sendNum + 12) = htobe32(self->lastRecvSerial_);
                    *(uint32_t *)(buf + sendNum + 16) = htobe32(++self->lastSendSerial_);
                }

                if (self->enableSendCRC_) {
                    //uint16_t crc = corpc::CRC::CheckSum(buf + sendNum, 0xFFFF, 20);
                    //crc = corpc::CRC::CheckSum(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, crc, msgSize);
                    //*(uint16_t *)(buf + sendNum + 20) = htobe16(crc);
                    uint16_t crc = corpc::CRC::CheckSum(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, 0xFFFF, msgSize);
                    *(uint16_t *)(buf + sendNum + 20) = htobe16(crc);
                }
                
                sendNum += msgSize + CORPC_MESSAGE_HEAD_SIZE;
                
                delete info;
                
                info = self->sendQueue_.pop();
                
                if (info) {
                    continue;
                }
            }
            
            int writeNum = 0;
            while (writeNum < sendNum) {
                ret = (int)write(s, buf + writeNum, sendNum);
                
                if (ret == -1) {
                    ERROR_LOG("write message\n");
                    self->close();
                    return nullptr;
                }
                
                writeNum += ret;
                if (writeNum < sendNum) {
                    // 等到能写时再继续
                    ret = poll(&fd_out, 1, -1);
                    
                    if (ret == -1) {
                        ERROR_LOG("poll fd_out\n");
                        self->close();
                        return nullptr;
                    }
                }
            }
            
            sendNum = 0;
        }
    }

    self->close();
    return nullptr;
}

bool UdpClient::start() {
    struct sockaddr_in si_me, si_other;
    socklen_t slen = sizeof(si_other);
    char buf[CORPC_MAX_UDP_MESSAGE_SIZE];
    
    if ((s_=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        ERROR_LOG("can't create socket\n");
        return false;
    }
    
    // zero out the structure
    memset((char *)&si_me, 0, sizeof(si_me));
    
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(local_port_);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int reuse = 1;
    setsockopt(s_, SOL_SOCKET, SO_REUSEADDR, &reuse,sizeof(reuse));
    setsockopt(s_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
    
    //bind socket to port
    if(bind(s_, (struct sockaddr*)&si_me, sizeof(si_me)) == -1) {
        ERROR_LOG("can't bind socket\n");
        return false;
    }
    
    memset((char *) &si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(port_);
    int nIP = inet_addr(host_.c_str());
    si_other.sin_addr.s_addr = nIP;
    
    if(connect(s_, (struct sockaddr *)&si_other, slen) == -1) {
        ERROR_LOG("can't connect\n");
        return false;
    }
    
    struct pollfd fd;
    int ret;
    
    fd.fd = s_;
    fd.events = POLLIN;
    
    // 准备握手消息
    char handshake1msg[CORPC_MESSAGE_HEAD_SIZE];
    char handshake3msg[CORPC_MESSAGE_HEAD_SIZE];
    
    memset(handshake1msg, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int32_t *)(handshake1msg + 4) = htobe32(CORPC_MSG_TYPE_UDP_HANDSHAKE_1);
    
    memset(handshake3msg, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int32_t *)(handshake3msg + 4) = htobe32(CORPC_MSG_TYPE_UDP_HANDSHAKE_3);
    
    // 握手阶段一：发送handshake1消息，然后等待handshake2消息到来，超时未到则重发handshake1消息
    while (true) {
        // 阶段一：发送handshake1消息
        if (write(s_, handshake1msg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
            ERROR_LOG("can't send handshake1\n");
            ::close(s_);
            return false;
        }
        DEBUG_LOG("send handshake1\n");

        // 阶段二：接收handshake2消息
        uint64_t nowms = mtime();
        uint64_t pollEndAt = nowms + 1000;
STEP1_POLLAGAIN:
        ret = poll(&fd, 1, pollEndAt - nowms); // 1 second for timeout
        switch (ret) {
            case -1:
                if (errno == EAGAIN || errno == EINTR) {
                    nowms = mtime();
                    if (nowms < pollEndAt) {
                        goto STEP1_POLLAGAIN;
                    }

                    continue;
                }

                ERROR_LOG("can't recv handshake2\n");
                ::close(s_);
                return false;
            case 0:
                continue; // 回阶段一
            default: {
                ret = (int)read(s_, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
                if (ret != CORPC_MESSAGE_HEAD_SIZE) {
                    ERROR_LOG("recv data size error\n");
                    ::close(s_);
                    return false;
                }
                
                int32_t msgtype = *(int32_t *)(buf + 4);
                msgtype = be32toh(msgtype);
                
                if (msgtype != CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                    ERROR_LOG("recv data not handshake2\n");
                    ::close(s_);
                    return false;
                }
                DEBUG_LOG("recv handshake2\n");
                goto STEP2;
            }
        }
    }

STEP2:
    while (true) {
        // 阶段三：发hankshake3消息
        if (write(s_, handshake3msg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
            ERROR_LOG("can't send handshake3\n");
            ::close(s_);
            return false;
        }
        DEBUG_LOG("send handshake3\n");

        // 阶段四：接收handshake4消息
        uint64_t nowms = mtime();
        uint64_t pollEndAt = nowms + 100;
STEP2_POLLAGAIN:
        ret = poll(&fd, 1, pollEndAt - nowms); // 100 millisecond for timeout
        switch (ret) {
            case -1:
                if (errno == EAGAIN || errno == EINTR) {
                    nowms = mtime();
                    if (nowms < pollEndAt) {
                        goto STEP2_POLLAGAIN;
                    }

                    continue;
                }

                ERROR_LOG("can't recv handshake4\n");
                ::close(s_);
                return false;
            case 0:
                continue; // 回阶段三
            default: {
                ret = (int)read(s_, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
                if (ret != CORPC_MESSAGE_HEAD_SIZE) {
                    ERROR_LOG("recv data size error\n");
                    ::close(s_);
                    return false;
                }
                
                int32_t msgtype = *(int32_t *)(buf + 4);
                msgtype = be32toh(msgtype);
                
                if (msgtype == CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                    continue;
                }

                if (msgtype != CORPC_MSG_TYPE_UDP_HANDSHAKE_4) {
                    ERROR_LOG("recv data not handshake4\n");
                    ::close(s_);
                    return false;
                }
                DEBUG_LOG("recv handshake4\n");
                goto STEP3;
            }
        }
    }

STEP3:
    if (needHB_) {
        struct timeval now = { 0 };
        gettimeofday( &now,NULL );
        lastRecvHBTime_ = now.tv_sec;
        lastRecvHBTime_ *= 1000;
        lastRecvHBTime_ += now.tv_usec / 1000;
        
        lastSendHBTime_ = lastRecvHBTime_;
    }
    
    running_ = true;
    // 启动数据收发协程
    RoutineEnvironment::startCoroutine(workRoutine, this);
    
    return true;
}

void *UdpClient::workRoutine( void * arg ) {
    std::shared_ptr<UdpClient> self = std::static_pointer_cast<UdpClient>(((UdpClient*)arg)->getPtr());

    std::string buffs(CORPC_MAX_UDP_MESSAGE_SIZE,0);
    uint8_t *buf = (uint8_t *)buffs.data();

    uint8_t heartbeatmsg[CORPC_MESSAGE_HEAD_SIZE];
    memset(heartbeatmsg, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int32_t *)(heartbeatmsg + 4) = htobe32(CORPC_MSG_TYPE_HEARTBEAT);

    int s = self->s_;
    struct pollfd fd;
    fd.fd = s;
    fd.events = POLLIN;
    
    int ret;
    
    uint32_t bodySize = 0;
    int32_t msgType = 0;
    uint16_t tag = 0;
    uint16_t flag = 0;
    
    uint64_t nowms = 0;
    // 开始定时心跳，以及接收／发送数据包
    // 逻辑：采用轮询机制
    //   1.每4ms尝试一次数据读取
    //   2.若数据是心跳，记录接收心跳的最新时间
    //   3.若超时未收到心跳，表示网络连接故障，进行断线处理
    //   4.若到心跳时间，发心跳
    //   5.查看是否有数据需要发送，有则发送
    //   6.回第1步
    while (self->running_) {
        ret = poll(&fd, 1, 4);

        struct timeval now = { 0 };
        gettimeofday( &now,NULL );
        nowms = now.tv_sec;
        nowms *= 1000;
        nowms += now.tv_usec / 1000;
        
        if (ret) {
            if (ret == -1) {
                if (errno == EAGAIN || errno == EINTR) {
                    continue;
                }

                ERROR_LOG("recv data\n");
                self->close();
                return nullptr;
            }
            
            ret = (int)read(s, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
            if (ret <= 0) {
                // ret 0 mean disconnected
                if (ret < 0 && (errno == EAGAIN || errno == EINTR)) {
                    continue;
                }
                
                ERROR_LOG("read data\n");
                self->close();
                return nullptr;
            } else {
                bodySize = *(uint32_t *)buf;
                bodySize = be32toh(bodySize);
                msgType = *(int32_t *)(buf + 4);
                msgType = be32toh(msgType);
                tag = *(uint16_t *)(buf + 8);
                tag = be16toh(tag);
                flag = *(uint16_t *)(buf + 10);
                flag = be16toh(flag);

                //DEBUG_LOG("recv msg type:%d\n", msgType);
                
                if (msgType < 0) {
                    if (msgType == CORPC_MSG_TYPE_HEARTBEAT) {
                        //printf("recv heartbeat\n");
                        self->lastRecvHBTime_ = nowms;
                    } else if (msgType == CORPC_MSG_TYPE_BANNED) {

                    } else {
                        ERROR_LOG("invalid msgType:%d fd:%d\n", msgType, s);
                        self->close();
                        return nullptr;
                    }
                } else {
                    assert(bodySize > 0);
                    // 校验序列号
                    if (self->enableSerial_) {
                        uint32_t serial = *(uint32_t *)(buf + 16);
                        serial = be32toh(serial);

                        if (serial != ++self->lastRecvSerial_) {
                            ERROR_LOG("serial check failed, fd:%d, need:%d, get:%d\n", s, self->lastRecvSerial_, serial);
                            self->close();
                            return nullptr;
                        }

                    }

                    // 校验CRC
                    if (self->enableRecvCRC_) {
                        uint16_t crc = *(uint16_t *)(buf + 20);
                        crc = be16toh(crc);

                        uint16_t crc1 = corpc::CRC::CheckSum(buf + CORPC_MESSAGE_HEAD_SIZE, 0xFFFF, bodySize);

                        if (crc != crc1) {
                            ERROR_LOG("crc check failed, fd:%d, recv:%d, cal:%d\n", s, crc, crc1);
                            self->close();
                            return nullptr;
                        }
                    }

                    // 解密
                    if ((flag & CORPC_MESSAGE_FLAG_CRYPT) != 0) {
                        if (self->crypter_ == nullptr) {
                            ERROR_LOG("cant decrypt message for crypter not exist, fd:%d\n", s);
                            self->close();
                            return nullptr;
                        }

                        self->crypter_->decrypt(buf + CORPC_MESSAGE_HEAD_SIZE, buf + CORPC_MESSAGE_HEAD_SIZE, bodySize);
                    }

                    // 解码数据
                    auto iter = self->registerMessageMap_.find(msgType);
                    if (iter == self->registerMessageMap_.end()) {
                        ERROR_LOG("unknown message: %d, fd:%d\n", msgType, s);
                        self->close();
                        return nullptr;
                    }
                    
                    std::shared_ptr<google::protobuf::Message> msg(iter->second.proto->New());
                    if (!msg->ParseFromArray(buf + CORPC_MESSAGE_HEAD_SIZE, bodySize)) {
                        // 出错处理
                        ERROR_LOG("parse body fail for message: %d, fd:%d\n", msgType, s);
                        self->close();
                        return nullptr;
                    }
                    
                    MessageInfo *info = new MessageInfo;
                    info->type = msgType;
                    info->tag = tag;
                    info->proto = msg;
                    
                    self->recvQueue_.push(info);
                }
            }
        }

        // 心跳判断
        if (nowms - self->lastRecvHBTime_ > CORPC_MAX_NO_HEARTBEAT_TIME) {
            // 无心跳，断线
            ERROR_LOG("heartbeat timeout, fd:%d\n", s);
            self->close();
            return nullptr;
        }
        
        if (nowms - self->lastSendHBTime_ > CORPC_HEARTBEAT_PERIOD) {
            DEBUG_LOG("send heartbeat, fd:%d\n", s);
            if (write(s, heartbeatmsg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
                ERROR_LOG("can't send heartbeat, fd:%d\n", s);
                self->close();
                return nullptr;
            }
            
            self->lastSendHBTime_ = nowms;
        }
        
        // 发送数据
        MessageInfo *info = self->sendQueue_.pop();
        while (info) {
            uint32_t msgSize = info->proto->ByteSizeLong();
            
            if (msgSize + CORPC_MESSAGE_HEAD_SIZE > CORPC_MAX_UDP_MESSAGE_SIZE) {
                ERROR_LOG("message size too large, fd:%d\n", s);
                self->close();
                return nullptr;
            }
            
            info->proto->SerializeWithCachedSizesToArray(buf + CORPC_MESSAGE_HEAD_SIZE);
            
            if (info->needCrypter) {
                assert(self->crypter_);
                self->crypter_->encrypt(buf + CORPC_MESSAGE_HEAD_SIZE, buf + CORPC_MESSAGE_HEAD_SIZE, msgSize);
                *(uint16_t *)(buf + 10) = htobe16(CORPC_MESSAGE_FLAG_CRYPT);
            } else {
                *(uint16_t *)(buf + 10) = 0;
            }

            *(uint32_t *)buf = htobe32(msgSize);
            *(int32_t *)(buf + 4) = htobe32(info->type);

            *(uint16_t *)(buf + 8) = htobe16(info->tag);

            if (self->enableSerial_) {
                *(uint32_t *)(buf + 12) = htobe32(self->lastRecvSerial_); // 由于UDP的包顺序会错乱，无法通过序号来删除缓存序号靠前的消息
                *(uint32_t *)(buf + 16) = htobe32(++self->lastSendSerial_);
            }

            if (self->enableSendCRC_) {
                //uint16_t crc = corpc::CRC::CheckSum(buf, 0xFFFF, CORPC_MESSAGE_HEAD_SIZE - 2);
                //crc = corpc::CRC::CheckSum(buf + CORPC_MESSAGE_HEAD_SIZE, crc, msgSize);
                uint16_t crc = corpc::CRC::CheckSum(buf + CORPC_MESSAGE_HEAD_SIZE, 0xFFFF, msgSize);

                *(uint16_t *)(buf + 20) = htobe16(crc);
            }
            
            int size = msgSize + CORPC_MESSAGE_HEAD_SIZE;
            //DEBUG_LOG("send message\n");
            if (write(s, buf, size) != size) {
                ERROR_LOG("can't send message, fd:%d\n", s);
                self->close();
                return nullptr;
            }
            
            delete info;
            
            info = self->sendQueue_.pop();
        }
    }

    self->close();
    return nullptr;
}

KcpClient::KcpClient(const std::string& host, uint16_t port, uint16_t local_port, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, std::shared_ptr<Crypter> crypter, uint32_t lastRecvSerial): MessageClient(needHB, enableSendCRC, enableRecvCRC, enableSerial, crypter, lastRecvSerial), host_(host), port_(port), local_port_(local_port) {
    pkcp_ = ikcp_create(0x1, (void *)this);
    ikcp_nodelay(pkcp_, 1, 20, 2, 1);
    pkcp_->output = rawOut;
}

KcpClient::~KcpClient() {
    DEBUG_LOG("KcpClient::~KcpClient\n");
    ikcp_release(pkcp_);
}

bool KcpClient::start() {
    struct sockaddr_in si_me, si_other;
    socklen_t slen = sizeof(si_other);
    char buf[CORPC_MAX_UDP_MESSAGE_SIZE];
    
    if ((s_=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        ERROR_LOG("can't create socket\n");
        return false;
    }
    
    // zero out the structure
    memset((char *)&si_me, 0, sizeof(si_me));
    
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(local_port_);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int reuse = 1;
    setsockopt(s_, SOL_SOCKET, SO_REUSEADDR, &reuse,sizeof(reuse));
    setsockopt(s_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
    
    //bind socket to port
    if(bind(s_, (struct sockaddr*)&si_me, sizeof(si_me)) == -1) {
        ERROR_LOG("can't bind socket\n");
        return false;
    }
    
    memset((char *) &si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(port_);
    int nIP = inet_addr(host_.c_str());
    si_other.sin_addr.s_addr = nIP;
    
    if(connect(s_, (struct sockaddr *)&si_other, slen) == -1) {
        ERROR_LOG("can't connect\n");
        return false;
    }
    
    struct pollfd fd;
    int ret;
    
    fd.fd = s_;
    fd.events = POLLIN;
    
    // 准备握手消息
    char handshake1msg[CORPC_MESSAGE_HEAD_SIZE];
    char handshake3msg[CORPC_MESSAGE_HEAD_SIZE];
    
    memset(handshake1msg, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int32_t *)(handshake1msg + 4) = htobe32(CORPC_MSG_TYPE_UDP_HANDSHAKE_1);
    
    memset(handshake3msg, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int32_t *)(handshake3msg + 4) = htobe32(CORPC_MSG_TYPE_UDP_HANDSHAKE_3);
    
    // 握手阶段一：发送handshake1消息，然后等待handshake2消息到来，超时未到则重发handshake1消息
    while (true) {
        // 阶段一：发送handshake1消息
        if (::write(s_, handshake1msg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
            ERROR_LOG("can't send handshake1\n");
            ::close(s_);
            return false;
        }
        DEBUG_LOG("send handshake1\n");
        
        // 阶段二：接收handshake2消息
        uint64_t nowms = mtime();
        uint64_t pollEndAt = nowms + 1000;
STEP1_POLLAGAIN:
        ret = poll(&fd, 1, pollEndAt - nowms); // 1 second for timeout
        switch (ret) {
            case -1:
                if (errno == EAGAIN || errno == EINTR) {
                    nowms = mtime();
                    if (nowms < pollEndAt) {
                        goto STEP1_POLLAGAIN;
                    }

                    continue;
                }

                ERROR_LOG("can't recv handshake2\n");
                ::close(s_);
                return false;
            case 0:
                continue; // 回阶段一
            default: {
                ret = (int)read(s_, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
                if (ret != CORPC_MESSAGE_HEAD_SIZE) {
                    ERROR_LOG("recv data size error\n");
                    ::close(s_);
                    return false;
                }
                
                int32_t msgtype = *(int32_t *)(buf + 4);
                msgtype = be32toh(msgtype);
                
                if (msgtype != CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                    ERROR_LOG("recv data not handshake2\n");
                    ::close(s_);
                    return false;
                }
                DEBUG_LOG("recv handshake2\n");
                goto STEP2;
            }
        }
    }

STEP2:
    while (true) {
        // 阶段三：发hankshake3消息
        if (::write(s_, handshake3msg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
            ERROR_LOG("can't send handshake3\n");
            ::close(s_);
            return false;
        }
        DEBUG_LOG("send handshake3\n");

        // 阶段四：接收handshake4消息
        uint64_t nowms = mtime();
        uint64_t pollEndAt = nowms + 100;
STEP2_POLLAGAIN:
        ret = poll(&fd, 1, pollEndAt - nowms); // 1 second for timeout
        switch (ret) {
            case -1:
                if (errno == EAGAIN || errno == EINTR) {
                    nowms = mtime();
                    if (nowms < pollEndAt) {
                        goto STEP2_POLLAGAIN;
                    }

                    continue;
                }

                ERROR_LOG("can't recv handshake4\n");
                ::close(s_);
                return false;
            case 0:
                continue; // 回阶段三
            default: {
                ret = (int)read(s_, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
                if (ret != CORPC_MESSAGE_HEAD_SIZE) {
                    ERROR_LOG("recv data size error\n");
                    ::close(s_);
                    return false;
                }
                
                int32_t msgtype = *(int32_t *)(buf + 4);
                msgtype = be32toh(msgtype);
                
                if (msgtype == CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                    continue;
                }

                if (msgtype != CORPC_MSG_TYPE_UDP_HANDSHAKE_4) {
                    ERROR_LOG("recv data not handshake4\n");
                    ::close(s_);
                    return false;
                }
                DEBUG_LOG("recv handshake4\n");
                goto STEP3;
            }
        }
    }

STEP3:
    if (needHB_) {
        struct timeval now = { 0 };
        gettimeofday( &now,NULL );
        lastRecvHBTime_ = now.tv_sec;
        lastRecvHBTime_ *= 1000;
        lastRecvHBTime_ += now.tv_usec / 1000;
        
        lastSendHBTime_ = lastRecvHBTime_;
    }
    
    running_ = true;
    // 启动数据收发协程
    RoutineEnvironment::startCoroutine(recvRoutine, this);
    RoutineEnvironment::startCoroutine(sendRoutine, this);
    RoutineEnvironment::startCoroutine(updateRoutine, this);
    
    return true;
}

void *KcpClient::recvRoutine(void *arg) {
    DEBUG_LOG("KcpClient::recvRoutine -- started\n");
    std::shared_ptr<KcpClient> self = std::static_pointer_cast<KcpClient>(((KcpClient*)arg)->getPtr());

    std::string buffs(CORPC_MESSAGE_HEAD_SIZE + CORPC_MAX_MESSAGE_SIZE,0);
    uint8_t *buf = (uint8_t *)buffs.data();

    std::string data(CORPC_MAX_KCP_PACKAGE_SIZE,0);
    uint8_t *dataBuf = (uint8_t *)data.data();

    int s = self->s_;
    
    int ret;
    
    std::string head(CORPC_MESSAGE_HEAD_SIZE,0);
    uint8_t *headBuf = (uint8_t *)head.data();
    
    std::string body(CORPC_MAX_MESSAGE_SIZE,0);
    uint8_t *bodyBuf = (uint8_t *)body.data();
    
    int headNum = 0;
    int bodyNum = 0;
    
    uint32_t bodySize = 0;
    int32_t msgType = 0;
    uint16_t tag = 0;
    uint16_t flag = 0;

    while (self->running_) {
        // 一次性读取尽可能多的数据
        ret = (int)read(s, buf, CORPC_MESSAGE_HEAD_SIZE + CORPC_MAX_MESSAGE_SIZE);
        if (ret <= 0) {
            // ret 0 mean disconnected
            if (ret < 0 && (errno == EAGAIN || errno == EINTR)) {
                continue;
            }

            DEBUG_LOG("read data error, fd: %d\n");
            goto END_LOOP;
        }

        // TODO: 如果是握手控制消息（消息长度为20），断开连接（因为这里不应该收到握手消息，很可能是服务端已经断开连接，
        // 后续发给服务器的消息服务器认为需要先握手）
        if (ret == CORPC_MESSAGE_HEAD_SIZE) {
            // 不是kcp数据消息，关闭连接
            ERROR_LOG("KcpClient::recvRoutine -- wrong msg\n");
            goto END_LOOP;
        }

        // 将收到的数据注入kcp中
        ret = ikcp_input(self->pkcp_, (const char*)buf, ret);
        if (ret < 0) {
            ERROR_LOG("KcpClient::recvRoutine -- ikcp_input failed\n");
            goto END_LOOP;
        }

        while (true)
        {
            //kcp将接收到的kcp数据包还原成之前kcp发送的buffer数据
            ret = ikcp_recv(self->pkcp_, (char*)dataBuf, CORPC_MAX_KCP_PACKAGE_SIZE);
            if (ret < 0) {
                if (ret == -1) {
                    break;
                }

                ERROR_LOG("KcpClient::recvRoutine -- ikcp_recv failed, ret:%d\n", ret);
                goto END_LOOP;
            }

            int remainNum = ret;
            int readNum = 0;
            
            while (remainNum) {
                // 解析消息头部
                if (headNum < CORPC_MESSAGE_HEAD_SIZE) {
                    if (remainNum <= CORPC_MESSAGE_HEAD_SIZE - headNum) {
                        memcpy(headBuf + headNum, dataBuf + readNum, remainNum);
                        readNum += remainNum;
                        headNum += remainNum;
                        remainNum = 0;
                    } else {
                        memcpy(headBuf + headNum, dataBuf + readNum, CORPC_MESSAGE_HEAD_SIZE - headNum);
                        readNum += CORPC_MESSAGE_HEAD_SIZE - headNum;
                        remainNum -= CORPC_MESSAGE_HEAD_SIZE - headNum;
                        headNum = CORPC_MESSAGE_HEAD_SIZE;
                    }
                    
                    if (headNum == CORPC_MESSAGE_HEAD_SIZE) {
                        bodySize = *(uint32_t *)headBuf;
                        bodySize = be32toh(bodySize);
                        msgType = *(int32_t *)(headBuf + 4);
                        msgType = be32toh(msgType);
                        tag = *(uint16_t *)(headBuf + 8);
                        tag = be16toh(tag);
                        flag = *(uint16_t *)(headBuf + 10);
                        flag = be16toh(flag);
                    } else {
                        assert(remainNum == 0);
                        break;
                    }
                }

                // 解析消息体
                if (remainNum > 0) {
                    if (bodyNum < bodySize) {
                        if (remainNum <= bodySize - bodyNum) {
                            memcpy(bodyBuf + bodyNum, dataBuf + readNum, remainNum);
                            readNum += remainNum;
                            bodyNum += remainNum;
                            remainNum = 0;
                        } else {
                            memcpy(bodyBuf + bodyNum, dataBuf + readNum, bodySize - bodyNum);
                            readNum += bodySize - bodyNum;
                            remainNum -= bodySize - bodyNum;
                            bodyNum = bodySize;
                        }
                    }
                }
                
                if (bodyNum == bodySize) {
                    // 根据消息类型解析和处理消息
                    if (msgType == CORPC_MSG_TYPE_HEARTBEAT) {
                        assert(bodySize == 0);
                        self->lastRecvHBTime_ = mtime();
                    } else if (msgType < 0) {
                        // 其他连接控制消息，不做处理
                    } else {
                        //assert(bodySize > 0);
                        // 校验序列号
                        if (self->enableSerial_) {
                            uint32_t serial = *(uint32_t *)(headBuf + 16);
                            serial = be32toh(serial);

                            if (serial != 0) {
                                if (serial != self->lastRecvSerial_ + 1) {
                                    ERROR_LOG("serial check failed, need:%d, get:%d\n", self->lastRecvSerial_+1, serial);
                                    goto END_LOOP;
                                }

                                self->lastRecvSerial_++;
                            }
                        }

                        if (bodySize > 0) {
                            // 校验CRC
                            if (self->enableRecvCRC_) {
                                uint16_t crc = *(uint16_t *)(headBuf + 20);
                                crc = be16toh(crc);

                                uint16_t crc1 = corpc::CRC::CheckSum(bodyBuf, 0xFFFF, bodySize);

                                if (crc != crc1) {
                                    ERROR_LOG("crc check failed, msgType:%d, size:%d, recv:%d, cal:%d\n", msgType, bodySize, crc, crc1);
                                    goto END_LOOP;
                                }
                            }

                            // 解密
                            if (flag & CORPC_MESSAGE_FLAG_CRYPT) {
                                if (self->crypter_ == nullptr) {
                                    ERROR_LOG("cant decrypt message for crypter not exist\n");
                                    goto END_LOOP;
                                }

                                self->crypter_->decrypt(bodyBuf, bodyBuf, bodySize);
                            }
                        }
                        
                        
                        // 解码数据
                        auto iter = self->registerMessageMap_.find(msgType);
                        if (iter == self->registerMessageMap_.end()) {
                            ERROR_LOG("unknown message: %d\n", msgType);
                            goto END_LOOP;
                        }
                        
                        std::shared_ptr<google::protobuf::Message> msg = nullptr;
                        if (bodySize > 0) {
                            msg.reset(iter->second.proto->New());
                            if (!msg->ParseFromArray(bodyBuf, bodySize)) {
                                // 出错处理
                                ERROR_LOG("parse body fail for message: %d\n", msgType);
                                goto END_LOOP;
                            }
                        }
                        
                        MessageInfo *info = new MessageInfo;
                        info->type = msgType;
                        info->tag = tag;
                        info->proto = msg;
                        
                        self->recvQueue_.push(info);
                    }
                    
                    // 处理完一个消息，需要清理变量
                    headNum = 0;
                    bodyNum = 0;
                    
                    msgType = 0;
                    bodySize = 0;
                }
            }
        }

        ikcp_flush(self->pkcp_);
    }

END_LOOP:

    self->close();

    DEBUG_LOG("KcpClient::recvRoutine -- stopped\n");
    return NULL;
}

void *KcpClient::sendRoutine(void *arg) {
    DEBUG_LOG("KcpClient::sendRoutine -- started\n");
    std::shared_ptr<KcpClient> self = std::static_pointer_cast<KcpClient>(((KcpClient*)arg)->getPtr());

    std::string buffs(CORPC_MESSAGE_HEAD_SIZE + CORPC_MAX_MESSAGE_SIZE,0);
    uint8_t *buf = (uint8_t *)buffs.data();

    auto& queue = self->sendQueue_;
    
    // 初始化pipe readfd
    int readFd = queue.getReadFd();
    co_register_fd(readFd);
    co_set_timeout(readFd, -1, 1000);
    
    int ret;
    while (self->running_) {
        // 等待处理信号
        ret = (int)read(readFd, buf, 1024);
        assert(ret != 0);
        if (ret < 0) {
            if (errno == EAGAIN) {
                continue;
            } else {
                // 管道出错
                ERROR_LOG("KcpClient::sendRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                       readFd, ret, errno, strerror(errno));
                
                // TODO: 如何处理？退出协程？
                // sleep 10 milisecond
                msleep(10);
            }
        }

        // 将要发送的数据拼在一起发送，提高效率
        int sendNum = 0;
        // 发送数据
        MessageInfo *info = self->sendQueue_.pop();
        while (info) {
            // TODO: 若是通知结束则退出

            uint32_t msgSize = info->proto->GetCachedSize();
            if (msgSize == 0) {
                msgSize = info->proto->ByteSizeLong();
            }
            
            if (msgSize + CORPC_MESSAGE_HEAD_SIZE > CORPC_MAX_MESSAGE_SIZE) {
                ERROR_LOG("message size too large\n");
                goto END_LOOP;
            }
            
            if (msgSize + CORPC_MESSAGE_HEAD_SIZE <= CORPC_MAX_MESSAGE_SIZE - sendNum) {
                info->proto->SerializeWithCachedSizesToArray(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE);

                if (info->needCrypter) {
                    assert(self->crypter_);
                    self->crypter_->encrypt(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, msgSize);
                    *(uint16_t *)(buf + sendNum + 10) = htobe16(CORPC_MESSAGE_FLAG_CRYPT);
                } else {
                    *(uint16_t *)(buf + sendNum + 10) = 0;
                }
                
                *(uint32_t *)(buf + sendNum) = htobe32(msgSize);
                *(int32_t *)(buf + sendNum + 4) = htobe32(info->type);

                *(uint16_t *)(buf + sendNum + 8) = htobe16(info->tag);
                
                if (self->enableSerial_) {
                    *(uint32_t *)(buf + sendNum + 12) = htobe32(self->lastRecvSerial_);
                    *(uint32_t *)(buf + sendNum + 16) = htobe32(++self->lastSendSerial_);
                }

                if (self->enableSendCRC_) {
                    uint16_t crc = corpc::CRC::CheckSum(buf + sendNum, 0xFFFF, 20);
                    crc = corpc::CRC::CheckSum(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, crc, msgSize);
                    *(uint16_t *)(buf + sendNum + 20) = htobe16(crc);
                }
                
                sendNum += msgSize + CORPC_MESSAGE_HEAD_SIZE;
                
            }
        
            delete info;
            
            // 发数据
            int ret = self->write(buf, sendNum);
            if (ret < 0) {
                goto END_LOOP;
            }
            
            sendNum = 0;

            info = self->sendQueue_.pop();
        }

        //uint64_t now = mtime();
        //uint32_t current = (uint32_t)(now & 0xfffffffful);
        //ikcp_update(self->pkcp_, current);
        ikcp_flush(self->pkcp_);
    }

END_LOOP:

    self->close();


    DEBUG_LOG("KcpClient::sendRoutine -- stopped\n");
    return NULL;
}

void *KcpClient::updateRoutine(void *arg) {
    DEBUG_LOG("KcpClient::updateRoutine -- started\n");
    std::shared_ptr<KcpClient> self = std::static_pointer_cast<KcpClient>(((KcpClient*)arg)->getPtr());

    uint8_t heartbeatmsg[CORPC_MESSAGE_HEAD_SIZE];
    memset(heartbeatmsg, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int32_t *)(heartbeatmsg + 4) = htobe32(CORPC_MSG_TYPE_HEARTBEAT);
    
    while (self->running_) {
        uint64_t now = mtime();
        // 心跳判断
        if (self->needHB_) {
            if (now - self->lastRecvHBTime_ > CORPC_MAX_NO_HEARTBEAT_TIME) {
                // 无心跳，断线
                ERROR_LOG("heartbeat timeout\n");
                self->close();
                return nullptr;
            }
            
            if (now - self->lastSendHBTime_ > CORPC_HEARTBEAT_PERIOD) {
                if (self->enableSerial_) {
                    *(uint32_t *)(heartbeatmsg + 12) = htobe32(self->lastRecvSerial_);
                }

                if (self->write(heartbeatmsg, CORPC_MESSAGE_HEAD_SIZE) < 0) {
                    ERROR_LOG("write heartbeat\n");
                    self->close();
                    return nullptr;
                }
                
                self->lastSendHBTime_ = now;
            }
        }

        uint32_t current = (uint32_t)(now & 0xfffffffful);
        ikcp_update(self->pkcp_, current);

        uint32_t next = ikcp_check(self->pkcp_, current);
        if (next > current) {
            // FixMe：这里会导致要发送的消息得不到立即发送
            //msleep(next-current);
            msleep(1);
        }
    }

    DEBUG_LOG("KcpClient::updateRoutine -- stopped\n");
    return NULL;
}

ssize_t KcpClient::write(const void *buf, size_t nbyte) {
    // 注意：这里分割数据，方便设定接收缓冲区大小
    int ret;
    uint32_t sentNum = 0;
    uint32_t leftNum = nbyte;

    do {
        uint32_t pkgSize = (leftNum > CORPC_MAX_KCP_PACKAGE_SIZE)?CORPC_MAX_KCP_PACKAGE_SIZE:leftNum;

        ret = ikcp_send(pkcp_, (const char *)(buf + sentNum), pkgSize);
        if (ret < 0) {
            WARN_LOG("KcpClient::write -- ret %d\n", ret);

            return ret;
        }

        sentNum += pkgSize;
        leftNum -= pkgSize;
    } while(leftNum > 0);

    return nbyte;
}

int KcpClient::rawOut(const char *buf, int len, ikcpcb *kcp, void *obj) {
    KcpClient* client = (KcpClient*)obj;
    int ret;
    uint32_t sentNum = 0;
    uint32_t leftNum = len;
    do {
        ret = (int)::write(client->s_, buf + sentNum, leftNum);
        if (ret > 0) {
            assert(ret <= leftNum);
            sentNum += ret;
            leftNum -= ret;
        }
    } while (leftNum > 0 && errno == EAGAIN);

    if (leftNum > 0) {
        WARN_LOG("KcpClient::rawOut -- write fd %d ret %d errno %d (%s)\n",
                   client->s_, ret, errno, strerror(errno));
        return -1;
    }
    
    assert(len == sentNum);
    return sentNum;
}

#endif