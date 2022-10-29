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
#include "corpc_crc.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

using namespace corpc;

//uint8_t MessageClient::_heartbeatmsg[CORPC_MESSAGE_HEAD_SIZE];

MessageClient::~MessageClient() {
    MessageInfo *info = _sendQueue.pop();
    while (info) {
        WARN_LOG("MessageClient::~MessageClient() something in send queue need to delete\n");
        delete info;
        info = _sendQueue.pop();
    }

    info = _recvQueue.pop();
    while (info) {
        WARN_LOG("MessageClient::~MessageClient() something in recv queue need to delete\n");
        delete info;
        info = _recvQueue.pop();
    }
}

void MessageClient::close() {
    //if (_running) {
    //    _running = false;
        LOG("MessageClient::close fd: %d\n", _s);
        ::close(_s);
    //}
}

void MessageClient::send(int16_t type, uint16_t tag, bool needCrypter, std::shared_ptr<google::protobuf::Message> msg) {
    MessageInfo *info = new MessageInfo;
    info->type = type;
    info->tag = tag;
    info->proto = msg;
    info->needCrypter = needCrypter;
    
    _sendQueue.push(info);
}

void MessageClient::recv(int16_t& type, uint16_t& tag, std::shared_ptr<google::protobuf::Message>& msg) {
    MessageInfo *info = _recvQueue.pop();
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

bool MessageClient::registerMessage(int16_t type, std::shared_ptr<google::protobuf::Message> proto) {
    MessageInfo info;
    info.type = type;
    info.proto = proto;
    
    _registerMessageMap.insert(std::make_pair(type, info));
    
    return false;
}

bool TcpClient::start() {
    if ((_s=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        ERROR_LOG("can't create socket\n");
        return false;
    }
    
    struct sockaddr_in addr;
    bzero(&addr,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_port);
    int nIP = inet_addr(_host.c_str());
    addr.sin_addr.s_addr = nIP;
    
    if (connect(_s, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        ERROR_LOG("can't connect\n");
        return false;
    }
    
    if (_needHB) {
        struct timeval now = { 0 };
        gettimeofday( &now,NULL );
        _lastRecvHBTime = now.tv_sec;
        _lastRecvHBTime *= 1000;
        _lastRecvHBTime += now.tv_usec / 1000;
        
        _lastSendHBTime = _lastRecvHBTime;
    }

    _running = true;
    
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
    *(int16_t *)(heartbeatmsg + 4) = htobe16(CORPC_MSG_TYPE_HEARTBEAT);
    
    int s = self->_s;
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
    int16_t msgType = 0;
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
    while (self->_running) {
        ret = poll(&fd_in, 1, 4);

        if (self->_needHB) {
            struct timeval now = { 0 };
            gettimeofday( &now,NULL );
            nowms = now.tv_sec;
            nowms *= 1000;
            nowms += now.tv_usec / 1000;
        }
        
        // BUG: 当连接在其他地方close时，poll会返回0，死循环。而且还会在文件描述符被重新打开时错误读取信息。
        if (ret) {
            if (ret < 0) {
                if (errno == EINTR) {
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
                        msgType = *(int16_t *)(headBuf + 4);
                        msgType = be16toh(msgType);
                        tag = *(uint16_t *)(headBuf + 6);
                        tag = be16toh(tag);
                        flag = *(uint16_t *)(headBuf + 8);
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
                        self->_lastRecvHBTime = nowms;
                    } else if (msgType < 0) {
                        // 其他连接控制消息，不做处理
                    } else {
                        //assert(bodySize > 0);
                        // 校验序列号
                        if (self->_enableSerial) {
                            uint32_t serial = *(uint32_t *)(headBuf + 14);
                            serial = be32toh(serial);

                            if (serial != 0) {
                                if (serial != self->_lastRecvSerial + 1) {
                                    ERROR_LOG("serial check failed, need:%d, get:%d\n", self->_lastRecvSerial+1, serial);
                                    self->close();
                                    return nullptr;
                                }

                                self->_lastRecvSerial++;
                            }
                        }

                        if (bodySize > 0) {
                            // 校验CRC
                            if (self->_enableRecvCRC) {
                                uint16_t crc = *(uint16_t *)(headBuf + 18);
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
                                if (self->_crypter == nullptr) {
                                    ERROR_LOG("cant decrypt message for crypter not exist\n");
                                    self->close();
                                    return nullptr;
                                }

                                self->_crypter->decrypt(bodyBuf, bodyBuf, bodySize);
                            }
                        }
                        
                        
                        // 解码数据
                        auto iter = self->_registerMessageMap.find(msgType);
                        if (iter == self->_registerMessageMap.end()) {
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
                        
                        self->_recvQueue.push(info);
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
        if (self->_needHB) {
            if (nowms - self->_lastRecvHBTime > CORPC_MAX_NO_HEARTBEAT_TIME) {
                // 无心跳，断线
                ERROR_LOG("heartbeat timeout\n");
                self->close();
                return nullptr;
            }
            
            if (nowms - self->_lastSendHBTime > CORPC_HEARTBEAT_PERIOD) {
                if (self->_enableSerial) {
                    *(uint32_t *)(heartbeatmsg + 10) = htobe32(self->_lastRecvSerial);
                }

                if (write(s, heartbeatmsg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
                    ERROR_LOG("write heartbeat\n");
                    self->close();
                    return nullptr;
                }
                
                self->_lastSendHBTime = nowms;
            }
        }

        // 将要发送的数据拼在一起发送，提高效率
        int sendNum = 0;
        // 发送数据
        MessageInfo *info = self->_sendQueue.pop();
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
                    assert(self->_crypter);
                    self->_crypter->encrypt(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, msgSize);
                    *(uint16_t *)(buf + sendNum + 8) = htobe16(CORPC_MESSAGE_FLAG_CRYPT);
                } else {
                    *(uint16_t *)(buf + sendNum + 8) = 0;
                }
                
                *(uint32_t *)(buf + sendNum) = htobe32(msgSize);
                *(int16_t *)(buf + sendNum + 4) = htobe16(info->type);

                *(uint16_t *)(buf + sendNum + 6) = htobe16(info->tag);
                
                if (self->_enableSerial) {
                    *(uint32_t *)(buf + sendNum + 10) = htobe32(self->_lastRecvSerial);
                    *(uint32_t *)(buf + sendNum + 14) = htobe32(++self->_lastSendSerial);
                }

                if (self->_enableSendCRC) {
                    uint16_t crc = corpc::CRC::CheckSum(buf + sendNum, 0xFFFF, 18);
                    crc = corpc::CRC::CheckSum(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, crc, msgSize);
                    *(uint16_t *)(buf + sendNum + 18) = htobe16(crc);
                }
                
                sendNum += msgSize + CORPC_MESSAGE_HEAD_SIZE;
                
                delete info;
                
                info = self->_sendQueue.pop();
                
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
    
    if ((_s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        ERROR_LOG("can't create socket\n");
        return false;
    }
    
    // zero out the structure
    memset((char *)&si_me, 0, sizeof(si_me));
    
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(_local_port);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int reuse = 1;
    setsockopt(_s, SOL_SOCKET, SO_REUSEADDR, &reuse,sizeof(reuse));
    setsockopt(_s, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
    
    //bind socket to port
    if(bind(_s, (struct sockaddr*)&si_me, sizeof(si_me)) == -1) {
        ERROR_LOG("can't bind socket\n");
        return false;
    }
    
    memset((char *) &si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(_port);
    int nIP = inet_addr(_host.c_str());
    si_other.sin_addr.s_addr = nIP;
    
    if(connect(_s, (struct sockaddr *)&si_other, slen) == -1) {
        ERROR_LOG("can't connect\n");
        return false;
    }
    
    struct pollfd fd;
    int ret;
    
    fd.fd = _s;
    fd.events = POLLIN;
    
    // 准备握手消息
    char handshake1msg[CORPC_MESSAGE_HEAD_SIZE];
    char handshake3msg[CORPC_MESSAGE_HEAD_SIZE];
    
    memset(handshake1msg, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int16_t *)(handshake1msg + 4) = htobe16(CORPC_MSG_TYPE_UDP_HANDSHAKE_1);
    
    memset(handshake3msg, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int16_t *)(handshake3msg + 4) = htobe16(CORPC_MSG_TYPE_UDP_HANDSHAKE_3);
    
    // 握手阶段一：发送handshake1消息，然后等待handshake2消息到来，超时未到则重发handshake1消息
    while (true) {
        // 阶段一：发送handshake1消息
        if (write(_s, handshake1msg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
            ERROR_LOG("can't send handshake1\n");
            ::close(_s);
            return false;
        }
        DEBUG_LOG("send handshake1\n");
        
        // 阶段二：接收handshake2消息
        ret = poll(&fd, 1, 1000); // 1 second for timeout
        switch (ret) {
            case -1:
                ERROR_LOG("can't recv handshake2\n");
                ::close(_s);
                return false;
            case 0:
                continue; // 回阶段一
            default: {
                ret = (int)read(_s, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
                if (ret != CORPC_MESSAGE_HEAD_SIZE) {
                    ERROR_LOG("recv data size error\n");
                    ::close(_s);
                    return false;
                }
                
                int16_t msgtype = *(int16_t *)(buf + 4);
                msgtype = be16toh(msgtype);
                
                if (msgtype != CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                    ERROR_LOG("recv data not handshake2\n");
                    ::close(_s);
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
        if (write(_s, handshake3msg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
            ERROR_LOG("can't send handshake3\n");
            ::close(_s);
            return false;
        }
        DEBUG_LOG("send handshake3\n");

        // 阶段四：接收handshake4消息
        ret = poll(&fd, 1, 100); // 100 millisecond for timeout
        switch (ret) {
            case -1:
                ERROR_LOG("can't recv handshake4\n");
                ::close(_s);
                return false;
            case 0:
                continue; // 回阶段三
            default: {
                ret = (int)read(_s, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
                if (ret != CORPC_MESSAGE_HEAD_SIZE) {
                    ERROR_LOG("recv data size error\n");
                    ::close(_s);
                    return false;
                }
                
                int16_t msgtype = *(int16_t *)(buf + 4);
                msgtype = be16toh(msgtype);
                
                if (msgtype == CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                    continue;
                }

                if (msgtype != CORPC_MSG_TYPE_UDP_HANDSHAKE_4) {
                    ERROR_LOG("recv data not handshake4\n");
                    ::close(_s);
                    return false;
                }
                DEBUG_LOG("recv handshake4\n");
                goto STEP3;
            }
        }
    }

STEP3:
    if (_needHB) {
        struct timeval now = { 0 };
        gettimeofday( &now,NULL );
        _lastRecvHBTime = now.tv_sec;
        _lastRecvHBTime *= 1000;
        _lastRecvHBTime += now.tv_usec / 1000;
        
        _lastSendHBTime = _lastRecvHBTime;
    }
    
    _running = true;
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
    *(int16_t *)(heartbeatmsg + 4) = htobe16(CORPC_MSG_TYPE_HEARTBEAT);

    int s = self->_s;
    struct pollfd fd;
    fd.fd = s;
    fd.events = POLLIN;
    
    int ret;
    
    uint32_t bodySize = 0;
    int16_t msgType = 0;
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
    while (self->_running) {
        ret = poll(&fd, 1, 4);

        struct timeval now = { 0 };
        gettimeofday( &now,NULL );
        nowms = now.tv_sec;
        nowms *= 1000;
        nowms += now.tv_usec / 1000;
        
        if (ret) {
            if (ret == -1) {
                ERROR_LOG("recv data\n");
                self->close();
                return nullptr;
            }
            
            ret = (int)read(s, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
            if (ret <= 0) {
                // ret 0 mean disconnected
                if (ret < 0 && errno == EAGAIN) {
                    continue;
                }
                
                ERROR_LOG("read data\n");
                self->close();
                return nullptr;
            } else {
                bodySize = *(uint32_t *)buf;
                bodySize = be32toh(bodySize);
                msgType = *(int16_t *)(buf + 4);
                msgType = be16toh(msgType);
                tag = *(uint16_t *)(buf + 6);
                tag = be16toh(tag);
                flag = *(uint16_t *)(buf + 8);
                flag = be16toh(flag);

                //DEBUG_LOG("recv msg type:%d\n", msgType);
                
                if (msgType < 0) {
                    if (msgType == CORPC_MSG_TYPE_HEARTBEAT) {
                        //printf("recv heartbeat\n");
                        self->_lastRecvHBTime = nowms;
                    } else if (msgType == CORPC_MSG_TYPE_BANNED) {

                    } else {
                        ERROR_LOG("invalid msgType:%d fd:%d\n", msgType, s);
                        self->close();
                        return nullptr;
                    }
                } else {
                    assert(bodySize > 0);
                    // 校验序列号
                    if (self->_enableSerial) {
                        uint32_t serial = *(uint32_t *)(buf + 14);
                        serial = be32toh(serial);

                        if (serial != ++self->_lastRecvSerial) {
                            ERROR_LOG("serial check failed, fd:%d, need:%d, get:%d\n", s, self->_lastRecvSerial, serial);
                            self->close();
                            return nullptr;
                        }

                    }

                    // 校验CRC
                    if (self->_enableRecvCRC) {
                        uint16_t crc = *(uint16_t *)(buf + 18);
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
                        if (self->_crypter == nullptr) {
                            ERROR_LOG("cant decrypt message for crypter not exist, fd:%d\n", s);
                            self->close();
                            return nullptr;
                        }

                        self->_crypter->decrypt(buf + CORPC_MESSAGE_HEAD_SIZE, buf + CORPC_MESSAGE_HEAD_SIZE, bodySize);
                    }

                    // 解码数据
                    auto iter = self->_registerMessageMap.find(msgType);
                    if (iter == self->_registerMessageMap.end()) {
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
                    
                    self->_recvQueue.push(info);
                }
            }
        }

        // 心跳判断
        if (nowms - self->_lastRecvHBTime > CORPC_MAX_NO_HEARTBEAT_TIME) {
            // 无心跳，断线
            ERROR_LOG("heartbeat timeout, fd:%d\n", s);
            self->close();
            return nullptr;
        }
        
        if (nowms - self->_lastSendHBTime > CORPC_HEARTBEAT_PERIOD) {
            DEBUG_LOG("send heartbeat, fd:%d\n", s);
            if (write(s, heartbeatmsg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
                ERROR_LOG("can't send heartbeat, fd:%d\n", s);
                self->close();
                return nullptr;
            }
            
            self->_lastSendHBTime = nowms;
        }
        
        // 发送数据
        MessageInfo *info = self->_sendQueue.pop();
        while (info) {
            uint32_t msgSize = info->proto->ByteSizeLong();
            
            if (msgSize + CORPC_MESSAGE_HEAD_SIZE > CORPC_MAX_UDP_MESSAGE_SIZE) {
                ERROR_LOG("message size too large, fd:%d\n", s);
                self->close();
                return nullptr;
            }
            
            info->proto->SerializeWithCachedSizesToArray(buf + CORPC_MESSAGE_HEAD_SIZE);
            
            if (info->needCrypter) {
                assert(self->_crypter);
                self->_crypter->encrypt(buf + CORPC_MESSAGE_HEAD_SIZE, buf + CORPC_MESSAGE_HEAD_SIZE, msgSize);
                *(uint16_t *)(buf + 8) = htobe16(CORPC_MESSAGE_FLAG_CRYPT);
            } else {
                *(uint16_t *)(buf + 8) = 0;
            }

            *(uint32_t *)buf = htobe32(msgSize);
            *(int16_t *)(buf + 4) = htobe16(info->type);

            *(uint16_t *)(buf + 6) = htobe16(info->tag);

            if (self->_enableSerial) {
                *(uint32_t *)(buf + 10) = htobe32(self->_lastRecvSerial); // 由于UDP的包顺序会错乱，无法通过序号来删除缓存序号靠前的消息
                *(uint32_t *)(buf + 14) = htobe32(++self->_lastSendSerial);
            }

            if (self->_enableSendCRC) {
                uint16_t crc = corpc::CRC::CheckSum(buf, 0xFFFF, CORPC_MESSAGE_HEAD_SIZE - 2);
                crc = corpc::CRC::CheckSum(buf + CORPC_MESSAGE_HEAD_SIZE, crc, msgSize);
                *(uint16_t *)(buf + 18) = htobe16(crc);
            }
            
            int size = msgSize + CORPC_MESSAGE_HEAD_SIZE;
            //DEBUG_LOG("send message\n");
            if (write(s, buf, size) != size) {
                ERROR_LOG("can't send message, fd:%d\n", s);
                self->close();
                return nullptr;
            }
            
            delete info;
            
            info = self->_sendQueue.pop();
        }
    }

    self->close();
    return nullptr;
}

KcpClient::KcpClient(const std::string& host, uint16_t port, uint16_t local_port, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, std::shared_ptr<Crypter> crypter, uint32_t lastRecvSerial): MessageClient(needHB, enableSendCRC, enableRecvCRC, enableSerial, crypter, lastRecvSerial), _host(host), _port(port), _local_port(local_port) {
    _pkcp = ikcp_create(0x1, (void *)this);
    ikcp_nodelay(_pkcp, 1, 20, 2, 1);
    _pkcp->output = rawOut;
}

KcpClient::~KcpClient() {
    DEBUG_LOG("KcpClient::~KcpClient\n");
    ikcp_release(_pkcp);
}

bool KcpClient::start() {
    struct sockaddr_in si_me, si_other;
    socklen_t slen = sizeof(si_other);
    char buf[CORPC_MAX_UDP_MESSAGE_SIZE];
    
    if ((_s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        ERROR_LOG("can't create socket\n");
        return false;
    }
    
    // zero out the structure
    memset((char *)&si_me, 0, sizeof(si_me));
    
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(_local_port);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int reuse = 1;
    setsockopt(_s, SOL_SOCKET, SO_REUSEADDR, &reuse,sizeof(reuse));
    setsockopt(_s, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
    
    //bind socket to port
    if(bind(_s, (struct sockaddr*)&si_me, sizeof(si_me)) == -1) {
        ERROR_LOG("can't bind socket\n");
        return false;
    }
    
    memset((char *) &si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(_port);
    int nIP = inet_addr(_host.c_str());
    si_other.sin_addr.s_addr = nIP;
    
    if(connect(_s, (struct sockaddr *)&si_other, slen) == -1) {
        ERROR_LOG("can't connect\n");
        return false;
    }
    
    struct pollfd fd;
    int ret;
    
    fd.fd = _s;
    fd.events = POLLIN;
    
    // 准备握手消息
    char handshake1msg[CORPC_MESSAGE_HEAD_SIZE];
    char handshake3msg[CORPC_MESSAGE_HEAD_SIZE];
    
    memset(handshake1msg, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int16_t *)(handshake1msg + 4) = htobe16(CORPC_MSG_TYPE_UDP_HANDSHAKE_1);
    
    memset(handshake3msg, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int16_t *)(handshake3msg + 4) = htobe16(CORPC_MSG_TYPE_UDP_HANDSHAKE_3);
    
    // 握手阶段一：发送handshake1消息，然后等待handshake2消息到来，超时未到则重发handshake1消息
    while (true) {
        // 阶段一：发送handshake1消息
        if (::write(_s, handshake1msg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
            ERROR_LOG("can't send handshake1\n");
            ::close(_s);
            return false;
        }
        DEBUG_LOG("send handshake1\n");
        
        // 阶段二：接收handshake2消息
        ret = poll(&fd, 1, 1000); // 1 second for timeout
        switch (ret) {
            case -1:
                ERROR_LOG("can't recv handshake2\n");
                ::close(_s);
                return false;
            case 0:
                continue; // 回阶段一
            default: {
                ret = (int)read(_s, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
                if (ret != CORPC_MESSAGE_HEAD_SIZE) {
                    ERROR_LOG("recv data size error\n");
                    ::close(_s);
                    return false;
                }
                
                int16_t msgtype = *(int16_t *)(buf + 4);
                msgtype = be16toh(msgtype);
                
                if (msgtype != CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                    ERROR_LOG("recv data not handshake2\n");
                    ::close(_s);
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
        if (::write(_s, handshake3msg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
            ERROR_LOG("can't send handshake3\n");
            ::close(_s);
            return false;
        }
        DEBUG_LOG("send handshake3\n");

        // 阶段四：接收handshake4消息
        ret = poll(&fd, 1, 100); // 100 millisecond for timeout
        switch (ret) {
            case -1:
                ERROR_LOG("can't recv handshake4\n");
                ::close(_s);
                return false;
            case 0:
                continue; // 回阶段三
            default: {
                ret = (int)read(_s, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
                if (ret != CORPC_MESSAGE_HEAD_SIZE) {
                    ERROR_LOG("recv data size error\n");
                    ::close(_s);
                    return false;
                }
                
                int16_t msgtype = *(int16_t *)(buf + 4);
                msgtype = be16toh(msgtype);
                
                if (msgtype == CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                    continue;
                }

                if (msgtype != CORPC_MSG_TYPE_UDP_HANDSHAKE_4) {
                    ERROR_LOG("recv data not handshake4\n");
                    ::close(_s);
                    return false;
                }
                DEBUG_LOG("recv handshake4\n");
                goto STEP3;
            }
        }
    }

STEP3:
    if (_needHB) {
        struct timeval now = { 0 };
        gettimeofday( &now,NULL );
        _lastRecvHBTime = now.tv_sec;
        _lastRecvHBTime *= 1000;
        _lastRecvHBTime += now.tv_usec / 1000;
        
        _lastSendHBTime = _lastRecvHBTime;
    }
    
    _running = true;
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

    int s = self->_s;
    
    int ret;
    
    std::string head(CORPC_MESSAGE_HEAD_SIZE,0);
    uint8_t *headBuf = (uint8_t *)head.data();
    
    std::string body(CORPC_MAX_MESSAGE_SIZE,0);
    uint8_t *bodyBuf = (uint8_t *)body.data();
    
    int headNum = 0;
    int bodyNum = 0;
    
    uint32_t bodySize = 0;
    int16_t msgType = 0;
    uint16_t tag = 0;
    uint16_t flag = 0;

    while (self->_running) {
//DEBUG_LOG("KcpClient::recvRoutine -- read\n");
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

//DEBUG_LOG("read data len:%d\n", ret);

        // 将收到的数据注入kcp中
        ret = ikcp_input(self->_pkcp, (const char*)buf, ret);
        if (ret < 0) {
            ERROR_LOG("KcpClient::recvRoutine -- ikcp_input failed\n");
            goto END_LOOP;
        }

//DEBUG_LOG("KcpClient::recvRoutine 1\n");

        while (true)
        {
            //kcp将接收到的kcp数据包还原成之前kcp发送的buffer数据
            ret = ikcp_recv(self->_pkcp, (char*)dataBuf, CORPC_MAX_KCP_PACKAGE_SIZE);
            if (ret < 0) {
                if (ret == -1) {
//DEBUG_LOG("KcpClient::recvRoutine 2\n");
                    break;
                }

                ERROR_LOG("KcpClient::recvRoutine -- ikcp_recv failed, ret:%d\n", ret);
                goto END_LOOP;
            }

//DEBUG_LOG("KcpClient::recvRoutine 3\n");
            int remainNum = ret;
            int readNum = 0;
            
            while (remainNum) {
//DEBUG_LOG("KcpClient::recvRoutine 4 %d\n", remainNum);
                // 解析消息头部
                if (headNum < CORPC_MESSAGE_HEAD_SIZE) {
//DEBUG_LOG("KcpClient::recvRoutine 5\n");
                    if (remainNum <= CORPC_MESSAGE_HEAD_SIZE - headNum) {
//DEBUG_LOG("KcpClient::recvRoutine 6\n");
                        memcpy(headBuf + headNum, dataBuf + readNum, remainNum);
                        readNum += remainNum;
                        headNum += remainNum;
                        remainNum = 0;
                    } else {
//DEBUG_LOG("KcpClient::recvRoutine 7\n");
                        memcpy(headBuf + headNum, dataBuf + readNum, CORPC_MESSAGE_HEAD_SIZE - headNum);
                        readNum += CORPC_MESSAGE_HEAD_SIZE - headNum;
                        remainNum -= CORPC_MESSAGE_HEAD_SIZE - headNum;
                        headNum = CORPC_MESSAGE_HEAD_SIZE;
                    }
                    
                    if (headNum == CORPC_MESSAGE_HEAD_SIZE) {
//DEBUG_LOG("KcpClient::recvRoutine 8\n");
                        bodySize = *(uint32_t *)headBuf;
                        bodySize = be32toh(bodySize);
                        msgType = *(int16_t *)(headBuf + 4);
                        msgType = be16toh(msgType);
                        tag = *(uint16_t *)(headBuf + 6);
                        tag = be16toh(tag);
                        flag = *(uint16_t *)(headBuf + 8);
                        flag = be16toh(flag);
                    } else {
//DEBUG_LOG("KcpClient::recvRoutine 9\n");
                        assert(remainNum == 0);
                        break;
                    }
                }
//DEBUG_LOG("KcpClient::recvRoutine 10\n");

                // 解析消息体
                if (remainNum > 0) {
//DEBUG_LOG("KcpClient::recvRoutine 11 %d\n", remainNum);
                    if (bodyNum < bodySize) {
                        if (remainNum <= bodySize - bodyNum) {
//DEBUG_LOG("KcpClient::recvRoutine 12\n");
                            memcpy(bodyBuf + bodyNum, dataBuf + readNum, remainNum);
                            readNum += remainNum;
                            bodyNum += remainNum;
                            remainNum = 0;
                        } else {
//DEBUG_LOG("KcpClient::recvRoutine 13\n");
                            memcpy(bodyBuf + bodyNum, dataBuf + readNum, bodySize - bodyNum);
                            readNum += bodySize - bodyNum;
                            remainNum -= bodySize - bodyNum;
                            bodyNum = bodySize;
                        }
                    }
                }
                
//DEBUG_LOG("KcpClient::recvRoutine 14\n");
                if (bodyNum == bodySize) {
//DEBUG_LOG("KcpClient::recvRoutine 15\n");
                    // 根据消息类型解析和处理消息
                    if (msgType == CORPC_MSG_TYPE_HEARTBEAT) {
//DEBUG_LOG("KcpClient::recvRoutine 16\n");
                        assert(bodySize == 0);
                        self->_lastRecvHBTime = mtime();
                    } else if (msgType < 0) {
//DEBUG_LOG("KcpClient::recvRoutine 17\n");
                        // 其他连接控制消息，不做处理
                    } else {
//DEBUG_LOG("KcpClient::recvRoutine 18\n");
                        //assert(bodySize > 0);
                        // 校验序列号
                        if (self->_enableSerial) {
                            uint32_t serial = *(uint32_t *)(headBuf + 14);
                            serial = be32toh(serial);

                            if (serial != 0) {
                                if (serial != self->_lastRecvSerial + 1) {
                                    ERROR_LOG("serial check failed, need:%d, get:%d\n", self->_lastRecvSerial+1, serial);
                                    goto END_LOOP;
                                }

                                self->_lastRecvSerial++;
                            }
                        }

                        if (bodySize > 0) {
                            // 校验CRC
                            if (self->_enableRecvCRC) {
                                uint16_t crc = *(uint16_t *)(headBuf + 18);
                                crc = be16toh(crc);

                                uint16_t crc1 = corpc::CRC::CheckSum(bodyBuf, 0xFFFF, bodySize);

                                if (crc != crc1) {
                                    ERROR_LOG("crc check failed, msgType:%d, size:%d, recv:%d, cal:%d\n", msgType, bodySize, crc, crc1);
                                    goto END_LOOP;                                }
                            }

                            // 解密
                            if (flag & CORPC_MESSAGE_FLAG_CRYPT) {
                                if (self->_crypter == nullptr) {
                                    ERROR_LOG("cant decrypt message for crypter not exist\n");
                                    goto END_LOOP;
                                }

                                self->_crypter->decrypt(bodyBuf, bodyBuf, bodySize);
                            }
                        }
                        
                        
                        // 解码数据
                        auto iter = self->_registerMessageMap.find(msgType);
                        if (iter == self->_registerMessageMap.end()) {
                            ERROR_LOG("unknown message: %d\n", msgType);
                            goto END_LOOP;                        }
                        
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
                        
                        self->_recvQueue.push(info);
                    }
                    
                    // 处理完一个消息，需要清理变量
                    headNum = 0;
                    bodyNum = 0;
                    
                    msgType = 0;
                    bodySize = 0;
                }
            }
        }

        ikcp_flush(self->_pkcp);
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

    auto& queue = self->_sendQueue;
    
    // 初始化pipe readfd
    int readFd = queue.getReadFd();
    co_register_fd(readFd);
    co_set_timeout(readFd, -1, 1000);
    
    int ret;
    while (self->_running) {
//DEBUG_LOG("KcpClient::sendRoutine 1\n");
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

        // 如果不是kcp消息（断线）
        if (ret == CORPC_MESSAGE_HEAD_SIZE) {
            ERROR_LOG("not kcp message\n");
            goto END_LOOP;
        }

        // 将要发送的数据拼在一起发送，提高效率
        int sendNum = 0;
        // 发送数据
        MessageInfo *info = self->_sendQueue.pop();
        while (info) {
//DEBUG_LOG("KcpClient::sendRoutine 2\n");
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
                    assert(self->_crypter);
                    self->_crypter->encrypt(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, msgSize);
                    *(uint16_t *)(buf + sendNum + 8) = htobe16(CORPC_MESSAGE_FLAG_CRYPT);
                } else {
                    *(uint16_t *)(buf + sendNum + 8) = 0;
                }
                
                *(uint32_t *)(buf + sendNum) = htobe32(msgSize);
                *(int16_t *)(buf + sendNum + 4) = htobe16(info->type);

                *(uint16_t *)(buf + sendNum + 6) = htobe16(info->tag);
                
                if (self->_enableSerial) {
                    *(uint32_t *)(buf + sendNum + 10) = htobe32(self->_lastRecvSerial);
                    *(uint32_t *)(buf + sendNum + 14) = htobe32(++self->_lastSendSerial);
                }

                if (self->_enableSendCRC) {
                    uint16_t crc = corpc::CRC::CheckSum(buf + sendNum, 0xFFFF, 18);
                    crc = corpc::CRC::CheckSum(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, crc, msgSize);
                    *(uint16_t *)(buf + sendNum + 18) = htobe16(crc);
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

            info = self->_sendQueue.pop();
        }

//DEBUG_LOG("KcpClient::sendRoutine 3\n");

        //uint64_t now = mtime();
        //uint32_t current = (uint32_t)(now & 0xfffffffful);
        //ikcp_update(self->_pkcp, current);
        ikcp_flush(self->_pkcp);
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
    *(int16_t *)(heartbeatmsg + 4) = htobe16(CORPC_MSG_TYPE_HEARTBEAT);
    
    while (self->_running) {
        uint64_t now = mtime();
        // 心跳判断
        if (self->_needHB) {
            if (now - self->_lastRecvHBTime > CORPC_MAX_NO_HEARTBEAT_TIME) {
                // 无心跳，断线
                ERROR_LOG("heartbeat timeout\n");
                self->close();
                return nullptr;
            }
            
            if (now - self->_lastSendHBTime > CORPC_HEARTBEAT_PERIOD) {
                if (self->_enableSerial) {
                    *(uint32_t *)(heartbeatmsg + 10) = htobe32(self->_lastRecvSerial);
                }

                if (self->write(heartbeatmsg, CORPC_MESSAGE_HEAD_SIZE) < 0) {
                    ERROR_LOG("write heartbeat\n");
                    self->close();
                    return nullptr;
                }
                
                self->_lastSendHBTime = now;
            }
        }

//DEBUG_LOG("KcpClient::updateRoutine -- ikcp_update\n");
        uint32_t current = (uint32_t)(now & 0xfffffffful);
        ikcp_update(self->_pkcp, current);

        uint32_t next = ikcp_check(self->_pkcp, current);
        if (next > current) {
            // FixMe：这里会导致要发送的消息得不到立即发送
            //msleep(next-current);
            msleep(1);
        }
    }

    DEBUG_LOG("KcpClient::updateRoutine -- stopped\n");
    return NULL;
}

/*
void *KcpClient::workRoutine( void * arg ) {
    DEBUG_LOG("KcpClient::workRoutine -- started\n");
    // TODO: 进行kcp改造
    std::shared_ptr<KcpClient> self = std::static_pointer_cast<KcpClient>(((KcpClient*)arg)->getPtr());

    std::string buffs(CORPC_MESSAGE_HEAD_SIZE + CORPC_MAX_MESSAGE_SIZE,0);
    uint8_t *buf = (uint8_t *)buffs.data();

    uint8_t heartbeatmsg[CORPC_MESSAGE_HEAD_SIZE];
    memset(heartbeatmsg, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int16_t *)(heartbeatmsg + 4) = htobe16(CORPC_MSG_TYPE_HEARTBEAT);
    
    std::string data(CORPC_MAX_KCP_PACKAGE_SIZE,0);
    uint8_t *dataBuf = (uint8_t *)data.data();

    int s = self->_s;
    struct pollfd fd;
    fd.fd = s;
    fd.events = POLLIN;
    
    int ret;
    
    std::string head(CORPC_MESSAGE_HEAD_SIZE,0);
    uint8_t *headBuf = (uint8_t *)head.data();
    
    std::string body(CORPC_MAX_MESSAGE_SIZE,0);
    uint8_t *bodyBuf = (uint8_t *)body.data();
    
    int headNum = 0;
    int bodyNum = 0;
    
    uint32_t bodySize = 0;
    int16_t msgType = 0;
    uint16_t tag = 0;
    uint16_t flag = 0;
    
    struct timeval now;
    uint64_t nowms = 0;

    uint32_t waitMs = 4;
    while (self->_running) {
DEBUG_LOG("KcpClient::workRoutine -- poll waitMs:%d\n", waitMs);
        ret = poll(&fd, 1, waitMs);

        if (self->_needHB) {
            gettimeofday( &now,NULL );
            nowms = now.tv_sec;
            nowms *= 1000;
            nowms += now.tv_usec / 1000;
        }
        
        // BUG: 当连接在其他地方close时，poll会返回0，死循环。而且还会在文件描述符被重新打开时错误读取信息。
        if (ret) {
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }

                DEBUG_LOG("poll fd error\n");
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

DEBUG_LOG("read data len:%d\n", ret);

            // 将收到的数据注入kcp中
            ret = ikcp_input(self->_pkcp, (const char*)buf, ret);
            if (ret < 0) {
                ERROR_LOG("KcpClient::workRoutine -- ikcp_input failed\n");
                self->close();
                return nullptr;
            }

DEBUG_LOG("KcpClient::workRoutine 1\n");

            while (true)
            {
                //kcp将接收到的kcp数据包还原成之前kcp发送的buffer数据
                ret = ikcp_recv(self->_pkcp, (char*)dataBuf, CORPC_MAX_KCP_PACKAGE_SIZE);
                if (ret < 0) {
                    if (ret == -1) {
DEBUG_LOG("KcpClient::workRoutine 2\n");

                        break;
                    }

                    ERROR_LOG("KcpClient::workRoutine -- ikcp_recv failed, ret:%d\n", ret);
                    self->close();
                    return nullptr;
                }

DEBUG_LOG("KcpClient::workRoutine 3\n");
                int remainNum = ret;
                int readNum = 0;
                
                while (remainNum) {
DEBUG_LOG("KcpClient::workRoutine 4 %d\n", remainNum);
                    // 解析消息头部
                    if (headNum < CORPC_MESSAGE_HEAD_SIZE) {
DEBUG_LOG("KcpClient::workRoutine 5\n");
                        if (remainNum <= CORPC_MESSAGE_HEAD_SIZE - headNum) {
DEBUG_LOG("KcpClient::workRoutine 6\n");
                            memcpy(headBuf + headNum, dataBuf + readNum, remainNum);
                            readNum += remainNum;
                            headNum += remainNum;
                            remainNum = 0;
                        } else {
DEBUG_LOG("KcpClient::workRoutine 7\n");
                            memcpy(headBuf + headNum, dataBuf + readNum, CORPC_MESSAGE_HEAD_SIZE - headNum);
                            readNum += CORPC_MESSAGE_HEAD_SIZE - headNum;
                            remainNum -= CORPC_MESSAGE_HEAD_SIZE - headNum;
                            headNum = CORPC_MESSAGE_HEAD_SIZE;
                        }
                        
                        if (headNum == CORPC_MESSAGE_HEAD_SIZE) {
DEBUG_LOG("KcpClient::workRoutine 8\n");
                            bodySize = *(uint32_t *)headBuf;
                            bodySize = be32toh(bodySize);
                            msgType = *(int16_t *)(headBuf + 4);
                            msgType = be16toh(msgType);
                            tag = *(uint16_t *)(headBuf + 6);
                            tag = be16toh(tag);
                            flag = *(uint16_t *)(headBuf + 8);
                            flag = be16toh(flag);
                        } else {
DEBUG_LOG("KcpClient::workRoutine 9\n");
                            assert(remainNum == 0);
                            break;
                        }
                    }
DEBUG_LOG("KcpClient::workRoutine 10\n");

                    // 解析消息体
                    if (remainNum > 0) {
DEBUG_LOG("KcpClient::workRoutine 11 %d\n", remainNum);
                        if (bodyNum < bodySize) {
                            if (remainNum <= bodySize - bodyNum) {
DEBUG_LOG("KcpClient::workRoutine 12\n");
                                memcpy(bodyBuf + bodyNum, dataBuf + readNum, remainNum);
                                readNum += remainNum;
                                bodyNum += remainNum;
                                remainNum = 0;
                            } else {
DEBUG_LOG("KcpClient::workRoutine 13\n");
                                memcpy(bodyBuf + bodyNum, dataBuf + readNum, bodySize - bodyNum);
                                readNum += bodySize - bodyNum;
                                remainNum -= bodySize - bodyNum;
                                bodyNum = bodySize;
                            }
                        }
                    }
                    
DEBUG_LOG("KcpClient::workRoutine 14\n");
                    if (bodyNum == bodySize) {
DEBUG_LOG("KcpClient::workRoutine 15\n");
                        // 根据消息类型解析和处理消息
                        if (msgType == CORPC_MSG_TYPE_HEARTBEAT) {
DEBUG_LOG("KcpClient::workRoutine 16\n");
                            assert(bodySize == 0);
                            self->_lastRecvHBTime = nowms;
                        } else if (msgType < 0) {
DEBUG_LOG("KcpClient::workRoutine 17\n");
                            // 其他连接控制消息，不做处理
                        } else {
DEBUG_LOG("KcpClient::workRoutine 18\n");
                            //assert(bodySize > 0);
                            // 校验序列号
                            if (self->_enableSerial) {
                                uint32_t serial = *(uint32_t *)(headBuf + 14);
                                serial = be32toh(serial);

                                if (serial != 0) {
                                    if (serial != self->_lastRecvSerial + 1) {
                                        ERROR_LOG("serial check failed, need:%d, get:%d\n", self->_lastRecvSerial+1, serial);
                                        self->close();
                                        return nullptr;
                                    }

                                    self->_lastRecvSerial++;
                                }
                            }

                            if (bodySize > 0) {
                                // 校验CRC
                                if (self->_enableRecvCRC) {
                                    uint16_t crc = *(uint16_t *)(headBuf + 18);
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
                                    if (self->_crypter == nullptr) {
                                        ERROR_LOG("cant decrypt message for crypter not exist\n");
                                        self->close();
                                        return nullptr;
                                    }

                                    self->_crypter->decrypt(bodyBuf, bodyBuf, bodySize);
                                }
                            }
                            
                            
                            // 解码数据
                            auto iter = self->_registerMessageMap.find(msgType);
                            if (iter == self->_registerMessageMap.end()) {
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
                            
                            self->_recvQueue.push(info);
                        }
                        
                        // 处理完一个消息，需要清理变量
                        headNum = 0;
                        bodyNum = 0;
                        
                        msgType = 0;
                        bodySize = 0;
                    }
                }
            }
        }
DEBUG_LOG("KcpClient::workRoutine 19\n");

        // 心跳判断
        if (self->_needHB) {
            if (nowms - self->_lastRecvHBTime > CORPC_MAX_NO_HEARTBEAT_TIME) {
                // 无心跳，断线
                ERROR_LOG("heartbeat timeout\n");
                self->close();
                return nullptr;
            }
            
            if (nowms - self->_lastSendHBTime > CORPC_HEARTBEAT_PERIOD) {
                if (self->_enableSerial) {
                    *(uint32_t *)(heartbeatmsg + 10) = htobe32(self->_lastRecvSerial);
                }

                if (self->write(heartbeatmsg, CORPC_MESSAGE_HEAD_SIZE) < 0) {
                    ERROR_LOG("write heartbeat\n");
                    self->close();
                    return nullptr;
                }
                
                self->_lastSendHBTime = nowms;
            }
        }
DEBUG_LOG("KcpClient::workRoutine 20\n");

        // 将要发送的数据拼在一起发送，提高效率
        int sendNum = 0;
        // 发送数据
        MessageInfo *info = self->_sendQueue.pop();
        while (info) {
DEBUG_LOG("KcpClient::workRoutine 21\n");
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
                    assert(self->_crypter);
                    self->_crypter->encrypt(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, msgSize);
                    *(uint16_t *)(buf + sendNum + 8) = htobe16(CORPC_MESSAGE_FLAG_CRYPT);
                } else {
                    *(uint16_t *)(buf + sendNum + 8) = 0;
                }
                
                *(uint32_t *)(buf + sendNum) = htobe32(msgSize);
                *(int16_t *)(buf + sendNum + 4) = htobe16(info->type);

                *(uint16_t *)(buf + sendNum + 6) = htobe16(info->tag);
                
                if (self->_enableSerial) {
                    *(uint32_t *)(buf + sendNum + 10) = htobe32(self->_lastRecvSerial);
                    *(uint32_t *)(buf + sendNum + 14) = htobe32(++self->_lastSendSerial);
                }

                if (self->_enableSendCRC) {
                    uint16_t crc = corpc::CRC::CheckSum(buf + sendNum, 0xFFFF, 18);
                    crc = corpc::CRC::CheckSum(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, crc, msgSize);
                    *(uint16_t *)(buf + sendNum + 18) = htobe16(crc);
                }
                
                sendNum += msgSize + CORPC_MESSAGE_HEAD_SIZE;
                
                delete info;
                
                info = self->_sendQueue.pop();
                
                if (info) {
                    continue;
                }
            }
            
            // 发数据
            int ret = self->write(buf, sendNum);
            if (ret < 0) {
                break;
            }
            
            sendNum = 0;
        }
DEBUG_LOG("KcpClient::workRoutine 22\n");

        gettimeofday(&now, NULL);
        uint64_t nowms = now.tv_sec * 1000 + now.tv_usec / 1000;
        uint32_t current = (uint32_t)(nowms & 0xfffffffful);
        ikcp_update(self->_pkcp, current);

        uint32_t next = ikcp_check(self->_pkcp, current);
        waitMs = (next > current) ? next-current:4;
    }

    self->close();
    return nullptr;
}
*/
ssize_t KcpClient::write(const void *buf, size_t nbyte) {
    // 注意：这里分割数据，方便设定接收缓冲区大小
    int ret;
    uint32_t sentNum = 0;
    uint32_t leftNum = nbyte;

    do {
        uint32_t pkgSize = (leftNum > CORPC_MAX_KCP_PACKAGE_SIZE)?CORPC_MAX_KCP_PACKAGE_SIZE:leftNum;

        ret = ikcp_send(_pkcp, (const char *)(buf + sentNum), pkgSize);
        if (ret < 0) {
            WARN_LOG("KcpMessageServer::Connection::write -- ret %d\n", ret);

            return ret;
        }

        sentNum += pkgSize;
        leftNum -= pkgSize;
    } while(leftNum > 0);

    return nbyte;
}

int KcpClient::rawOut(const char *buf, int len, ikcpcb *kcp, void *obj) {
//DEBUG_LOG("KcpClient::rawOut begin\n");
    KcpClient* client = (KcpClient*)obj;
    int ret;
    uint32_t sentNum = 0;
    uint32_t leftNum = len;
    do {
        ret = (int)::write(client->_s, buf + sentNum, leftNum);
        if (ret > 0) {
            assert(ret <= leftNum);
            sentNum += ret;
            leftNum -= ret;
        }
    } while (leftNum > 0 && errno == EAGAIN);

    if (leftNum > 0) {
        WARN_LOG("KcpClient::rawOut -- write fd %d ret %d errno %d (%s)\n",
                   client->_s, ret, errno, strerror(errno));
        return -1;
    }
//DEBUG_LOG("KcpClient::rawOut end\n");
    assert(len == sentNum);
    return sentNum;
}