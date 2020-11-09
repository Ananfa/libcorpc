//
//  client1.cpp
//  echoTcp
//
//  Created by Xianke Liu on 2018/5/4.
//Copyright © 2018年 Dena. All rights reserved.
//

#include <string>
#include <thread>
#include <list>
#include <mutex>
#include <memory.h>
#include <stdio.h> //printf
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/time.h>

#include <google/protobuf/message.h>
#include "corpc_crypter.h"
#include "corpc_crc.h"
#include "echo.pb.h"

#define CORPC_MSG_TYPE_BANNED -10
#define CORPC_MSG_TYPE_HEARTBEAT -115
#define CORPC_MESSAGE_FLAG_CRYPT 0x1

#define CORPC_MESSAGE_HEAD_SIZE 12
#define CORPC_MAX_MESSAGE_SIZE 0x10000

#define CORPC_HEARTBEAT_PERIOD 5000
#define CORPC_MAX_NO_HEARTBEAT_TIME 15000

template <typename T>
class SyncQueue {
public:
    SyncQueue() {}
    ~SyncQueue() {}
    
    void push(T & v) {
        std::unique_lock<std::mutex> lock( _queueMutex );
        _inqueue.push_back(v);
    }
    
    void push(T && v) {
        std::unique_lock<std::mutex> lock( _queueMutex );
        _inqueue.push_back(std::move(v));
    }
    
    T pop() {
        T ret(nullptr);
        
        if (!_outqueue.empty()) {
            ret = std::move(_outqueue.front());
            
            _outqueue.pop_front();
        } else {
            if (!_inqueue.empty()) {
                {
                    std::unique_lock<std::mutex> lock( _queueMutex );
                    _inqueue.swap(_outqueue);
                }
                
                ret = std::move(_outqueue.front());
                
                _outqueue.pop_front();
            }
        }
        
        return ret;
    }
    
private:
    std::mutex _queueMutex;
    std::list<T> _inqueue;
    std::list<T> _outqueue;
};

class TcpClient {
    struct MessageInfo {
        int16_t type;
        google::protobuf::Message *proto;
    };
    
public:
    TcpClient(const std::string& host, uint16_t port, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, std::shared_ptr<corpc::Crypter> &crypter): _host(host), _port(port), _needHB(needHB), _enableSendCRC(enableSendCRC), _enableRecvCRC(enableRecvCRC), _enableSerial(enableSerial), _crypter(crypter), _lastRecvHBTime(0), _lastSendHBTime(0) {}
    ~TcpClient() {}
    
    bool start();
    
    void join();
    
    void send(int32_t type, google::protobuf::Message* msg);
    void recv(int32_t& type, google::protobuf::Message*& msg); // 收不到数据时type为0，msg为nullptr
    
    bool registerMessage(int type,
                         google::protobuf::Message *proto);
    
private:
    static void threadEntry( TcpClient *self ); // 数据收发线程
    
private:
    std::string _host;
    uint16_t _port;
    
    bool _needHB;        // 是否需要心跳
    bool _enableSendCRC; // 是否需要发包时校验CRC码
    bool _enableRecvCRC; // 是否需要收包时校验CRC码
    bool _enableSerial;  // 是否需要消息序号
    
    std::shared_ptr<corpc::Crypter> _crypter;

    int _s;
    std::thread _t;
    
    SyncQueue<MessageInfo*> _sendQueue;
    SyncQueue<MessageInfo*> _recvQueue;
    
    std::map<int, MessageInfo> _registerMessageMap;
    
    uint64_t _lastRecvHBTime; // 最后一次收到心跳的时间
    uint64_t _lastSendHBTime; // 最后一次发送心跳的时间
};

bool TcpClient::start() {
    if ((_s=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        perror("can't create socket");
        return false;
    }
    
    struct sockaddr_in addr;
    bzero(&addr,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_port);
    int nIP = inet_addr(_host.c_str());
    addr.sin_addr.s_addr = nIP;
    
    if (connect(_s, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("can't connect");
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

void TcpClient::threadEntry( TcpClient *self ) {
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
    uint16_t flag = 0;

    uint16_t lastRecvSerial = 0;
    uint16_t lastSendSerial = 0;
    
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

                perror("poll fd_in\n");
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
                
                perror("read data\n");
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
                        flag = *(uint16_t *)(buf + 6);
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
                            uint16_t serial = *(uint16_t *)(buf + 8);
                            serial = be16toh(serial);

                            if (serial != ++lastRecvSerial) {
                                printf("ERROR: serial check failed, need:%d, get:%d\n", lastRecvSerial, serial);
                                close(s);
                                return;
                            }

                        }

                        // 校验CRC
                        if (self->_enableRecvCRC) {
                            uint16_t crc = *(uint16_t *)(buf + 10);
                            crc = be16toh(crc);

                            uint16_t crc1 = corpc::CRC::CheckSum(bodyBuf, 0xFFFF, bodySize);

                            if (crc != crc1) {
                                printf("ERROR: crc check failed, recv:%d, cal:%d\n", crc, crc1);
                                close(s);
                                return;
                            }
                        }

                        // 解密
                        if (flag & CORPC_MESSAGE_FLAG_CRYPT != 0) {
                            if (self->_crypter == nullptr) {
                                printf("ERROR: cant decrypt message for crypter not exist\n");
                                close(s);
                                return;
                            }

                            self->_crypter->decrypt(bodyBuf, bodyBuf, bodySize);
                        }
                        
                        // 解码数据
                        auto iter = self->_registerMessageMap.find(msgType);
                        if (iter == self->_registerMessageMap.end()) {
                            printf("ERROR: unknown message: %d\n", msgType);
                            close(s);
                            return;
                        }
                        
                        google::protobuf::Message *msg = iter->second.proto->New();
                        if (!msg->ParseFromArray(bodyBuf, bodySize)) {
                            // 出错处理
                            printf("ERROR: parse body fail for message: %d\n", msgType);
                            close(s);
                            return;
                        }
                        
                        MessageInfo *info = new MessageInfo;
                        info->type = msgType;
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
                printf("ERROR: heartbeat timeout");
                close(s);
                return;
            }
            
            if (nowms - self->_lastSendHBTime > CORPC_HEARTBEAT_PERIOD) {
                if (write(s, heartbeatmsg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
                    perror("write heartbeat");
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
                msgSize = info->proto->ByteSize();
            }
            
            if (msgSize + CORPC_MESSAGE_HEAD_SIZE > CORPC_MAX_MESSAGE_SIZE) {
                printf("message size too large\n");
                close(s);
                return;
            }
            
            if (msgSize + CORPC_MESSAGE_HEAD_SIZE <= CORPC_MAX_MESSAGE_SIZE - sendNum) {
                info->proto->SerializeWithCachedSizesToArray(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE);

                if (self->_crypter != nullptr) {
                    self->_crypter->encrypt(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, msgSize);

                    uint16_t flag = CORPC_MESSAGE_FLAG_CRYPT;
                    *(uint16_t *)(buf + sendNum + 6) = htobe16(flag);
                }
                
                *(uint32_t *)(buf + sendNum) = htobe32(msgSize);
                *(int16_t *)(buf + sendNum + 4) = htobe16(info->type);

                if (self->_enableSerial) {
                    *(uint16_t *)(buf + sendNum + 8) = htobe16(++lastSendSerial);
                }

                if (self->_enableSendCRC) {
                    uint16_t crc = corpc::CRC::CheckSum(buf + sendNum + 8, 0xFFFF, 2);
                    crc = corpc::CRC::CheckSum(buf + sendNum + CORPC_MESSAGE_HEAD_SIZE, crc, msgSize);
                    *(uint16_t *)(buf + sendNum + 10) = htobe16(crc);
                }
                
                sendNum += msgSize + CORPC_MESSAGE_HEAD_SIZE;
                
                delete info->proto;
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
                    perror("write message");
                    close(s);
                    return;
                }
                
                writeNum += ret;
                if (writeNum < sendNum) {
                    // 等到能写时再继续
                    ret = poll(&fd_out, 1, -1);
                    
                    if (ret == -1) {
                        perror("poll fd_out");
                        close(s);
                        return;
                    }
                }
            }
            
            sendNum = 0;
        }
    }
}

void TcpClient::join() {
    _t.join();
}

void TcpClient::send(int32_t type, google::protobuf::Message* msg) {
    MessageInfo *info = new MessageInfo;
    info->type = type;
    info->proto = msg;
    
    _sendQueue.push(info);
}

void TcpClient::recv(int32_t& type, google::protobuf::Message*& msg) {
    MessageInfo *info = _recvQueue.pop();
    if (info) {
        type = info->type;
        msg = info->proto;
        
        delete info;
    } else {
        type = 0;
        msg = nullptr;
    }
}

bool TcpClient::registerMessage(int type, google::protobuf::Message *proto) {
    MessageInfo info;
    info.type = type;
    info.proto = proto;
    
    _registerMessageMap.insert(std::make_pair(type, info));
    
    return false;
}

void testThread(std::string host, uint16_t port) {
    std::string key("1234567fvxcvc");
    std::shared_ptr<corpc::Crypter> crypter = std::shared_ptr<corpc::Crypter>(new corpc::SimpleXORCrypter(key));
    TcpClient client(host, port, true, true, true, true, crypter);
    client.registerMessage(1, new FooResponse);
    client.registerMessage(CORPC_MSG_TYPE_BANNED, new BanResponse);
    
    client.start();
    
    // send/recv data to/from client
    while (true) {
        // send FooRequest
        FooRequest *request = new FooRequest;
        request->set_text("hello world!");
        request->set_times(10);
        
        client.send(1, request);
        
        int32_t rType;
        google::protobuf::Message *rMsg;
        do {
            usleep(100);
            client.recv(rType, rMsg);
        } while (!rType);
        
        switch (rType) {
            case 1: {
                FooResponse *response = (FooResponse*)rMsg;
                //printf("%s\n", response->text().c_str());
                delete response;
                break;
            }
            case 2: {
                BanResponse *response = (BanResponse*)rMsg;
                //printf("%s\n", response->text().c_str());
                delete response;
                break;
            }
            default:
                assert(false);
        }
    }
}

int main(int argc, const char * argv[])
{
    if(argc<3){
        printf("Usage:\n"
               "echoUdpclt [HOST] [PORT]\n");
        return -1;
    }
    
    std::string host = argv[1];
    uint16_t port = atoi(argv[2]);
    
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigaction( SIGPIPE, &sa, NULL );

    // 启动多个线程创建client
    int clientNum = 20;
    std::vector<std::thread> threads;
    for (int i = 0; i < clientNum; i++) {
        threads.push_back(std::thread(testThread, host, port));
    }
    
    for (int i = 0; i < clientNum; i++) {
        threads[i].join();
    }
    
    return 0;
}


