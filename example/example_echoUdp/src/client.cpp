/*
 * Created by Xianke Liu on 2018/4/28.
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

#include <string>
#include <thread>
#include <list>
#include <mutex>
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

#define LOCAL_PORT 20000

#define CORPC_MSG_TYPE_UDP_UNSHAKE -110
#define CORPC_MSG_TYPE_UDP_HANDSHAKE_1 -111
#define CORPC_MSG_TYPE_UDP_HANDSHAKE_2 -112
#define CORPC_MSG_TYPE_UDP_HANDSHAKE_3 -113
#define CORPC_MSG_TYPE_HEARTBEAT -115
#define CORPC_MESSAGE_FLAG_CRYPT 0x1

#define CORPC_MESSAGE_HEAD_SIZE 12
#define CORPC_MAX_UDP_MESSAGE_SIZE 540

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

class UdpClient {
    struct MessageInfo {
        int32_t type;
        google::protobuf::Message *proto;
    };

public:
    UdpClient(const std::string& host, uint16_t port, uint16_t local_port, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, std::shared_ptr<corpc::Crypter> crypter): _host(host), _port(port), _local_port(local_port), _needHB(needHB), _enableSendCRC(enableSendCRC), _enableRecvCRC(enableRecvCRC), _enableSerial(enableSerial), _crypter(crypter), _lastRecvHBTime(0), _lastSendHBTime(0) {}
    ~UdpClient() {}
    
    bool start();
    
    void join();
    
    void send(int32_t type, google::protobuf::Message* msg);
    void recv(int32_t& type, google::protobuf::Message*& msg); // 收不到数据时type为0，msg为nullptr
    
    bool registerMessage(int type,
                         google::protobuf::Message *proto);
    
private:
    static void threadEntry( UdpClient *self ); // 数据收发线程
    
private:
    std::string _host;
    uint16_t _port;
    uint16_t _local_port;
    
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

bool UdpClient::start() {
    struct sockaddr_in si_me, si_other;
    socklen_t slen = sizeof(si_other);
    char buf[CORPC_MAX_UDP_MESSAGE_SIZE];
    
    if ((_s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        perror("can't create socket");
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
        perror("can't bind socket");
        return false;
    }
    
    memset((char *) &si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(_port);
    int nIP = inet_addr(_host.c_str());
    si_other.sin_addr.s_addr = nIP;
    
    if(connect(_s, (struct sockaddr *)&si_other, slen) == -1) {
        perror("can't connect");
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
            perror("can't send handshake1");
            close(_s);
            return false;
        }
        
        // 阶段二：接收handshake2消息
        ret = poll(&fd, 1, 1000); // 1 second for timeout
        switch (ret) {
            case -1:
                perror("can't recv handshake2");
                close(_s);
                return false;
            case 0:
                continue; // 回阶段一
            default: {
                ret = (int)read(_s, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
                if (ret != CORPC_MESSAGE_HEAD_SIZE) {
                    perror("recv data size error");
                    close(_s);
                    return false;
                }
                
                int16_t msgtype = *(int16_t *)(buf + 4);
                msgtype = be16toh(msgtype);
                
                if (msgtype != CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                    perror("recv data not handshake2");
                    close(_s);
                    return false;
                }
                
                break;
            }
        }
        
        // 阶段三：发hankshake3消息
        if (write(_s, handshake3msg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
            perror("can't send handshake3");
            close(_s);
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
}

void UdpClient::threadEntry( UdpClient *self ) {
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
    
    uint16_t lastRecvSerial = 0;
    uint16_t lastSendSerial = 0;
    
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
        uint64_t nowms = now.tv_sec;
        nowms *= 1000;
        nowms += now.tv_usec / 1000;
        
        if (ret) {
            if (ret == -1) {
                perror("recv data");
                close(s);
                return;
            }
            
            ret = (int)read(s, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
            if (ret <= 0) {
                // ret 0 mean disconnected
                if (ret < 0 && errno == EAGAIN) {
                    continue;
                }
                
                perror("read data");
                close(s);
                return;
            } else {
                uint32_t bodySize = *(uint32_t *)buf;
                bodySize = be32toh(bodySize);
                int16_t msgType = *(int16_t *)(buf + 4);
                msgType = be16toh(msgType);
                uint16_t flag = *(uint16_t *)(buf + 6);
                flag = be16toh(flag);
                
                if (msgType < 0) {
                    if (msgType == CORPC_MSG_TYPE_HEARTBEAT) {
                        //printf("recv heartbeat\n");
                        self->_lastRecvHBTime = nowms;
                    } else if (msgType == CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                        // 重发handshake_3
                        if (write(s, handshake3msg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
                            perror("can't send handshake3");
                            close(s);
                            return;
                        }
                    } else if (msgType == CORPC_MSG_TYPE_UDP_UNSHAKE) {
                        perror("unshake");
                        close(s);
                        return;
                    }
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

                        uint16_t crc1 = corpc::CRC::CheckSum(buf + CORPC_MESSAGE_HEAD_SIZE, 0xFFFF, bodySize);

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

                        self->_crypter->decrypt(buf + CORPC_MESSAGE_HEAD_SIZE, buf + CORPC_MESSAGE_HEAD_SIZE, bodySize);
                    }

                    // 解码数据
                    auto iter = self->_registerMessageMap.find(msgType);
                    if (iter == self->_registerMessageMap.end()) {
                        printf("ERROR: unknown message: %d\n", msgType);
                        close(s);
                        return;
                    }
                    
                    google::protobuf::Message *msg = iter->second.proto->New();
                    if (!msg->ParseFromArray(buf + CORPC_MESSAGE_HEAD_SIZE, bodySize)) {
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
            }
        }
        
        // 心跳判断
        if (nowms - self->_lastRecvHBTime > CORPC_MAX_NO_HEARTBEAT_TIME) {
            // 无心跳，断线
            printf("ERROR: heartbeat timeout");
            close(s);
            return;
        }
        
        if (nowms - self->_lastSendHBTime > CORPC_HEARTBEAT_PERIOD) {
            if (write(s, heartbeatmsg, CORPC_MESSAGE_HEAD_SIZE) != CORPC_MESSAGE_HEAD_SIZE) {
                printf("can't send heartbeat");
                close(s);
                return;
            }
            
            self->_lastSendHBTime = nowms;
            //printf("send heartbeat\n");
        }
        
        // 发送数据
        MessageInfo *info = self->_sendQueue.pop();
        while (info) {
            uint32_t msgSize = info->proto->ByteSize();
            
            if (msgSize + CORPC_MESSAGE_HEAD_SIZE > CORPC_MAX_UDP_MESSAGE_SIZE) {
                printf("message size too large");
                close(s);
                return;
            }
            
            info->proto->SerializeWithCachedSizesToArray(buf + CORPC_MESSAGE_HEAD_SIZE);
            
            if (self->_crypter != nullptr) {
                self->_crypter->encrypt(buf + CORPC_MESSAGE_HEAD_SIZE, buf + CORPC_MESSAGE_HEAD_SIZE, msgSize);

                uint16_t flag = CORPC_MESSAGE_FLAG_CRYPT;
                *(uint16_t *)(buf + 6) = htobe16(flag);
            }

            *(uint32_t *)buf = htobe32(msgSize);
            *(int16_t *)(buf + 4) = htobe16(info->type);

            if (self->_enableSerial) {
                *(uint16_t *)(buf + 8) = htobe16(++lastSendSerial);
            }

            if (self->_enableSendCRC) {
                uint16_t crc = corpc::CRC::CheckSum(buf + 8, 0xFFFF, 2);
                crc = corpc::CRC::CheckSum(buf + CORPC_MESSAGE_HEAD_SIZE, crc, msgSize);
                *(uint16_t *)(buf + 10) = htobe16(crc);
            }
            
            int size = msgSize + CORPC_MESSAGE_HEAD_SIZE;
            if (write(s, buf, size) != size) {
                printf("can't send message");
                close(s);
                return;
            }
            
            delete info->proto;
            delete info;
            
            info = self->_sendQueue.pop();
        }
    }
}

void UdpClient::join() {
    _t.join();
}

void UdpClient::send(int32_t type, google::protobuf::Message* msg) {
    MessageInfo *info = new MessageInfo;
    info->type = type;
    info->proto = msg;
    
    _sendQueue.push(info);
}

void UdpClient::recv(int32_t& type, google::protobuf::Message*& msg) {
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

bool UdpClient::registerMessage(int type, google::protobuf::Message *proto) {
    MessageInfo info;
    info.type = type;
    info.proto = proto;
    
    _registerMessageMap.insert(std::make_pair(type, info));
    
    return false;
}

void testThread(std::string host, uint16_t port, uint16_t local_port) {
    std::string key("1234567fvxcvc");
    std::shared_ptr<corpc::Crypter> crypter = std::shared_ptr<corpc::Crypter>(new corpc::SimpleXORCrypter(key));
    UdpClient client(host, port, local_port, true, true, true, false, crypter);
    client.registerMessage(1, new FooResponse);
    
    client.start();
    
    // send/recv data to/from client
    while (true) {
        // send FooRequest
        FooRequest *request = new FooRequest;
        request->set_text("hello world!");
        request->set_times(2);
        
        client.send(1, request);
        
        int32_t rType;
        google::protobuf::Message *rMsg;
        do {
            usleep(100);
            client.recv(rType, rMsg);
        } while (!rType);
        
        assert(rType == 1);
        FooResponse *response = (FooResponse*)rMsg;
        //printf("%s\n", response->text().c_str());
        delete response;
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
        threads.push_back(std::thread(testThread, host, port, LOCAL_PORT+i));
    }
    
    for (int i = 0; i < clientNum; i++) {
        threads[i].join();
    }
    
    return 0;
}

