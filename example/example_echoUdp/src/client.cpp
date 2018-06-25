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
#include "echo.pb.h"

#define LOCAL_PORT 20000

#define CORPC_MSG_TYPE_UDP_HANDSHAKE_1 -111
#define CORPC_MSG_TYPE_UDP_HANDSHAKE_2 -112
#define CORPC_MSG_TYPE_UDP_HANDSHAKE_3 -113
#define CORPC_MSG_TYPE_HEARTBEAT -115

#define CORPC_MESSAGE_HEAD_SIZE 8
#define CORPC_MAX_UDP_MESSAGE_SIZE 540

#define CORPC_HEARTBEAT_PERIOD 10000
#define CORPC_MAX_NO_HEARTBEAT_TIME 30000

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
    UdpClient(const std::string& host, uint16_t port, uint16_t local_port): _host(host), _port(port), _local_port(local_port), _lastRecvHBTime(0), _lastSendHBTime(0) {}
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
    
    *(uint32_t *)handshake1msg = htonl(0);
    *(uint32_t *)(handshake1msg + 4) = htonl(CORPC_MSG_TYPE_UDP_HANDSHAKE_1);
    
    *(uint32_t *)handshake3msg = htonl(0);
    *(uint32_t *)(handshake3msg + 4) = htonl(CORPC_MSG_TYPE_UDP_HANDSHAKE_3);
    
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
                if (ret != 8) {
                    perror("recv data size error");
                    close(_s);
                    return false;
                }
                
                int32_t msgtype = *(int32_t *)(buf + 4);
                msgtype = ntohl(msgtype);
                
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
        
        struct timeval now = { 0 };
        gettimeofday( &now,NULL );
        _lastRecvHBTime = now.tv_sec;
        _lastRecvHBTime *= 1000;
        _lastRecvHBTime += now.tv_usec / 1000;
        
        _lastSendHBTime = _lastRecvHBTime;
        
        // 启动数据收发线程
        _t = std::thread(threadEntry, this);
        
        return true;
    }
}

void UdpClient::threadEntry( UdpClient *self ) {
    uint8_t buf[CORPC_MAX_UDP_MESSAGE_SIZE];
    uint8_t heartbeatmsg[CORPC_MESSAGE_HEAD_SIZE];
    *(uint32_t *)heartbeatmsg = htonl(0);
    *(uint32_t *)(heartbeatmsg + 4) = htonl(CORPC_MSG_TYPE_HEARTBEAT);
    
    int s = self->_s;
    struct pollfd fd;
    fd.fd = s;
    fd.events = POLLIN;
    
    int ret;
    
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
                bodySize = ntohl(bodySize);
                int32_t msgType = *(int32_t *)(buf + 4);
                msgType = ntohl(msgType);
                
                if (msgType < 0) {
                    if (msgType == CORPC_MSG_TYPE_HEARTBEAT) {
                        //printf("recv heartbeat\n");
                        self->_lastRecvHBTime = nowms;
                    }
                } else {
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
            
            *(uint32_t *)buf = htonl(msgSize);
            *(uint32_t *)(buf + 4) = htonl(info->type);
            
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
    UdpClient client(host, port, local_port);
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

