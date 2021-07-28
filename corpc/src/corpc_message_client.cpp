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


void MessageClient::join() {
    _t.join();
}

void MessageClient::detach() {
    _t.detach();
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
    
    // 启动数据收发线程
    _t = std::thread(threadEntry, this);
    
    return true;
}

void TcpClient::threadEntry( TcpClient *client ) {
    std::shared_ptr<TcpClient> self = std::static_pointer_cast<TcpClient>(client->getPtr());

    uint8_t buf[CORPC_MAX_MESSAGE_SIZE];
    
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

    uint32_t lastRecvSerial = 0;
    uint32_t lastSendSerial = 0;
    
    uint64_t nowms = 0;
    // 开始定时心跳，以及接收／发送数据包
    // 逻辑：采用轮询机制
    //   1.每4ms尝试一次数据读取
    //   2.若数据是心跳，记录接收心跳的最新时间
    //   3.若超时未收到心跳，表示网络连接故障，进行断线处理
    //   4.若到心跳时间，发心跳
    //   5.查看是否有数据需要发送，有则发送
    //   6.回第1步
    while (true) {
        ret = poll(&fd_in, 1, 4);
        
        if (self->_needHB) {
            struct timeval now = { 0 };
            gettimeofday( &now,NULL );
            nowms = now.tv_sec;
            nowms *= 1000;
            nowms += now.tv_usec / 1000;
        }
        
        if (ret) {
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }

                ERROR_LOG("poll fd_in\n");
                close(s);
                return;
            }
            
            // 一次性读取尽可能多的数据
            ret = (int)read(s, buf, CORPC_MESSAGE_HEAD_SIZE + CORPC_MAX_MESSAGE_SIZE);
            if (ret <= 0) {
                // ret 0 mean disconnected
                if (ret < 0 && (errno == EAGAIN || errno == EINTR)) {
                    continue;
                }
                
                ERROR_LOG("read data\n");
                close(s);
                return;
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
                        bodySize = *(uint32_t *)buf;
                        bodySize = be32toh(bodySize);
                        msgType = *(int16_t *)(buf + 4);
                        msgType = be16toh(msgType);
                        tag = *(uint16_t *)(buf + 6);
                        tag = be16toh(tag);
                        flag = *(uint16_t *)(buf + 8);
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
                        assert(bodySize > 0);
                        // 校验序列号
                        if (self->_enableSerial) {
                            uint32_t serial = *(uint32_t *)(buf + 14);
                            serial = be32toh(serial);

                            if (serial != ++lastRecvSerial) {
                                ERROR_LOG("serial check failed, need:%d, get:%d\n", lastRecvSerial, serial);
                                close(s);
                                return;
                            }

                        }

                        // 校验CRC
                        if (self->_enableRecvCRC) {
                            uint16_t crc = *(uint16_t *)(buf + 18);
                            crc = be16toh(crc);

                            uint16_t crc1 = corpc::CRC::CheckSum(bodyBuf, 0xFFFF, bodySize);

                            if (crc != crc1) {
                                ERROR_LOG("crc check failed, msgType:%d, size:%d, recv:%d, cal:%d\n", msgType, bodySize, crc, crc1);
                                close(s);
                                return;
                            }
                        }

                        // 解密
                        if ((flag & CORPC_MESSAGE_FLAG_CRYPT) != 0) {
                            if (self->_crypter == nullptr) {
                                ERROR_LOG("cant decrypt message for crypter not exist\n");
                                close(s);
                                return;
                            }

                            self->_crypter->decrypt(bodyBuf, bodyBuf, bodySize);
                        }
                        
                        // 解码数据
                        auto iter = self->_registerMessageMap.find(msgType);
                        if (iter == self->_registerMessageMap.end()) {
                            ERROR_LOG("unknown message: %d\n", msgType);
                            close(s);
                            return;
                        }
                        
                        std::shared_ptr<google::protobuf::Message> msg(iter->second.proto->New());
                        if (!msg->ParseFromArray(bodyBuf, bodySize)) {
                            // 出错处理
                            ERROR_LOG("parse body fail for message: %d\n", msgType);
                            close(s);
                            return;
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
                close(s);
                return;
            }
            
            if (nowms - self->_lastSendHBTime > CORPC_HEARTBEAT_PERIOD) {
                if (self->_enableSerial) {
                    *(uint32_t *)(heartbeatmsg + 10) = htobe32(lastRecvSerial);
                }

                if (write(s, heartbeatmsg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
                    ERROR_LOG("write heartbeat\n");
                    close(s);
                    return;
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
                close(s);
                return;
            }
            
            if (msgSize + CORPC_MESSAGE_HEAD_SIZE <= CORPC_MAX_MESSAGE_SIZE - sendNum) {
                info->proto->SerializeWithCachedSizesToArray(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE);

                if (info->needCrypter) {
                    assert(self->_crypter);
                    self->_crypter->encrypt(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, msgSize);
                    *(uint16_t *)(buf + sendNum + 8) = htobe16(CORPC_MESSAGE_FLAG_CRYPT);
                }
                
                *(uint32_t *)(buf + sendNum) = htobe32(msgSize);
                *(int16_t *)(buf + sendNum + 4) = htobe16(info->type);

                *(uint16_t *)(buf + sendNum + 6) = htobe16(info->tag);
                
                if (self->_enableSerial) {
                    *(uint32_t *)(buf + sendNum + 10) = htobe32(lastRecvSerial);
                    *(uint32_t *)(buf + sendNum + 14) = htobe32(++lastSendSerial);
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
                    close(s);
                    return;
                }
                
                writeNum += ret;
                if (writeNum < sendNum) {
                    // 等到能写时再继续
                    ret = poll(&fd_out, 1, -1);
                    
                    if (ret == -1) {
                        ERROR_LOG("poll fd_out\n");
                        close(s);
                        return;
                    }
                }
            }
            
            sendNum = 0;
        }
    }
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
            close(_s);
            return false;
        }
        DEBUG_LOG("send handshake1\n");
        
        // 阶段二：接收handshake2消息
        ret = poll(&fd, 1, 1000); // 1 second for timeout
        switch (ret) {
            case -1:
                ERROR_LOG("can't recv handshake2\n");
                close(_s);
                return false;
            case 0:
                continue; // 回阶段一
            default: {
                ret = (int)read(_s, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
                if (ret != CORPC_MESSAGE_HEAD_SIZE) {
                    ERROR_LOG("recv data size error\n");
                    close(_s);
                    return false;
                }
                
                int16_t msgtype = *(int16_t *)(buf + 4);
                msgtype = be16toh(msgtype);
                
                if (msgtype != CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                    ERROR_LOG("recv data not handshake2\n");
                    close(_s);
                    return false;
                }
                DEBUG_LOG("recv handshake2\n");
                break;
            }
        }
        
        // 阶段三：发hankshake3消息
        if (write(_s, handshake3msg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
            ERROR_LOG("can't send handshake3\n");
            close(_s);
            return false;
        }
        DEBUG_LOG("send handshake3\n");
        
        if (_needHB) {
            struct timeval now = { 0 };
            gettimeofday( &now,NULL );
            _lastRecvHBTime = now.tv_sec;
            _lastRecvHBTime *= 1000;
            _lastRecvHBTime += now.tv_usec / 1000;
            
            _lastSendHBTime = _lastRecvHBTime;
        }
        
        // 启动数据收发线程
        _t = std::thread(threadEntry, this);
        
        return true;
    }
}

void UdpClient::threadEntry( UdpClient *client ) {
    std::shared_ptr<UdpClient> self = std::static_pointer_cast<UdpClient>(client->getPtr());

    uint8_t buf[CORPC_MAX_UDP_MESSAGE_SIZE];
    uint8_t heartbeatmsg[CORPC_MESSAGE_HEAD_SIZE];
    memset(heartbeatmsg, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int16_t *)(heartbeatmsg + 4) = htobe16(CORPC_MSG_TYPE_HEARTBEAT);
    
    char handshake3msg[CORPC_MESSAGE_HEAD_SIZE];
    memset(handshake3msg, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int16_t *)(handshake3msg + 4) = htobe16(CORPC_MSG_TYPE_UDP_HANDSHAKE_3);

    int s = self->_s;
    struct pollfd fd;
    fd.fd = s;
    fd.events = POLLIN;
    
    int ret;
    
    uint32_t bodySize = 0;
    int16_t msgType = 0;
    uint16_t tag = 0;
    uint16_t flag = 0;

    uint32_t lastRecvSerial = 0;
    uint32_t lastSendSerial = 0;
    
    uint64_t nowms = 0;

    bool shakeOK = false;
    // 开始定时心跳，以及接收／发送数据包
    // 逻辑：采用轮询机制
    //   1.每4ms尝试一次数据读取
    //   2.若数据是心跳，记录接收心跳的最新时间
    //   3.若超时未收到心跳，表示网络连接故障，进行断线处理
    //   4.若到心跳时间，发心跳
    //   5.查看是否有数据需要发送，有则发送
    //   6.回第1步
    while (true) {
        ret = poll(&fd, 1, 4);

        struct timeval now = { 0 };
        gettimeofday( &now,NULL );
        nowms = now.tv_sec;
        nowms *= 1000;
        nowms += now.tv_usec / 1000;
        
        if (ret) {
            if (ret == -1) {
                ERROR_LOG("recv data\n");
                close(s);
                return;
            }
            
            ret = (int)read(s, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
            if (ret <= 0) {
                // ret 0 mean disconnected
                if (ret < 0 && errno == EAGAIN) {
                    continue;
                }
                
                ERROR_LOG("read data\n");
                close(s);
                return;
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
                    } else if (msgType == CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                        // 重发handshake_3
                        DEBUG_LOG("resend handshake3, fd:%d\n", s);
                        if (write(s, handshake3msg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
                            ERROR_LOG("can't send handshake3, fd:%d\n", s);
                            close(s);
                            return;
                        }
                    } else if (msgType == CORPC_MSG_TYPE_UDP_HANDSHAKE_4) {
                        shakeOK = true;
                        DEBUG_LOG("shake succeed, fd:%d\n", s);
                    } else if (msgType == CORPC_MSG_TYPE_UDP_UNSHAKE) {
                        ERROR_LOG("unshake, fd:%d\n", s);
                        close(s);
                        return;
                    }
                } else if (!shakeOK) {
                    ERROR_LOG("recv msg when unshake, fd:%d\n", s);
                    close(s);
                    return;
                } else {
                    assert(bodySize > 0);
                    // 校验序列号
                    if (self->_enableSerial) {
                        uint32_t serial = *(uint32_t *)(buf + 14);
                        serial = be32toh(serial);

                        if (serial != ++lastRecvSerial) {
                            ERROR_LOG("serial check failed, fd:%d, need:%d, get:%d\n", s, lastRecvSerial, serial);
                            close(s);
                            return;
                        }

                    }

                    // 校验CRC
                    if (self->_enableRecvCRC) {
                        uint16_t crc = *(uint16_t *)(buf + 18);
                        crc = be16toh(crc);

                        uint16_t crc1 = corpc::CRC::CheckSum(buf + CORPC_MESSAGE_HEAD_SIZE, 0xFFFF, bodySize);

                        if (crc != crc1) {
                            ERROR_LOG("crc check failed, fd:%d, recv:%d, cal:%d\n", s, crc, crc1);
                            close(s);
                            return;
                        }
                    }

                    // 解密
                    if ((flag & CORPC_MESSAGE_FLAG_CRYPT) != 0) {
                        if (self->_crypter == nullptr) {
                            ERROR_LOG("cant decrypt message for crypter not exist, fd:%d\n", s);
                            close(s);
                            return;
                        }

                        self->_crypter->decrypt(buf + CORPC_MESSAGE_HEAD_SIZE, buf + CORPC_MESSAGE_HEAD_SIZE, bodySize);
                    }

                    // 解码数据
                    auto iter = self->_registerMessageMap.find(msgType);
                    if (iter == self->_registerMessageMap.end()) {
                        ERROR_LOG("unknown message: %d, fd:%d\n", msgType, s);
                        close(s);
                        return;
                    }
                    
                    std::shared_ptr<google::protobuf::Message> msg(iter->second.proto->New());
                    if (!msg->ParseFromArray(buf + CORPC_MESSAGE_HEAD_SIZE, bodySize)) {
                        // 出错处理
                        ERROR_LOG("parse body fail for message: %d, fd:%d\n", msgType, s);
                        close(s);
                        return;
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
            close(s);
            return;
        }
        
        if (!shakeOK) {
            continue;
        }
        
        if (nowms - self->_lastSendHBTime > CORPC_HEARTBEAT_PERIOD) {
            DEBUG_LOG("send heartbeat, fd:%d\n", s);
            if (write(s, heartbeatmsg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
                ERROR_LOG("can't send heartbeat, fd:%d\n", s);
                close(s);
                return;
            }
            
            self->_lastSendHBTime = nowms;
        }
        
        // 发送数据
        MessageInfo *info = self->_sendQueue.pop();
        while (info) {
            uint32_t msgSize = info->proto->ByteSizeLong();
            
            if (msgSize + CORPC_MESSAGE_HEAD_SIZE > CORPC_MAX_UDP_MESSAGE_SIZE) {
                ERROR_LOG("message size too large, fd:%d\n", s);
                close(s);
                return;
            }
            
            info->proto->SerializeWithCachedSizesToArray(buf + CORPC_MESSAGE_HEAD_SIZE);
            
            if (self->_crypter != nullptr) {
                self->_crypter->encrypt(buf + CORPC_MESSAGE_HEAD_SIZE, buf + CORPC_MESSAGE_HEAD_SIZE, msgSize);

                uint16_t flag = CORPC_MESSAGE_FLAG_CRYPT;
                *(uint16_t *)(buf + 8) = htobe16(flag);
            }

            *(uint32_t *)buf = htobe32(msgSize);
            *(int16_t *)(buf + 4) = htobe16(info->type);

            *(uint16_t *)(buf + 6) = htobe16(info->tag);

            if (self->_enableSerial) {
                *(uint32_t *)(buf + 14) = htobe32(++lastSendSerial);
            }

            if (self->_enableSendCRC) {
                uint16_t crc = corpc::CRC::CheckSum(buf, 0xFFFF, 18);
                crc = corpc::CRC::CheckSum(buf + CORPC_MESSAGE_HEAD_SIZE, crc, msgSize);
                *(uint16_t *)(buf + 18) = htobe16(crc);
            }
            
            int size = msgSize + CORPC_MESSAGE_HEAD_SIZE;
            //DEBUG_LOG("send message\n");
            if (write(s, buf, size) != size) {
                ERROR_LOG("can't send message, fd:%d\n", s);
                close(s);
                return;
            }
            
            delete info;
            
            info = self->_sendQueue.pop();
        }
    }
}
