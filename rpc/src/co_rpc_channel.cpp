//
//  co_rpc_channel.cpp
//  rpccli
//
//  Created by Xianke Liu on 2017/10/27.
//  Copyright © 2017年 Dena. All rights reserved.
//

#include "co_rpc_routine_env.h"
#include "co_rpc_channel.h"
#include "co_rpc_client.h"

#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>

#include <google/protobuf/message.h>

#include "corpc_option.pb.h"

namespace CoRpc {
    
    void Connection::start(const stCoRoutineAttr_t *attr) {
        // 启动连接侦听协程建立与服务器的连接
        assert(_deamonCo == NULL);
        _deamonCo = RoutineEnvironment::startCoroutine(workingRoutine, this);
    }
    
    void * Connection::workingRoutine( void * arg ) {
        co_enable_hook_sys();
        Connection *self = (Connection *)arg;
        self->_deamonHang = false;
        int ret = 0;
        
        while (true) {
            assert(self->_st != CONNECTING);
            if (self->_st == CONNECTED) {
                if (self->_waitSendTaskCoList.empty() && self->_waitResultCoMap.empty()) {
                    self->_deamonHang = true;
                    co_yield_ct();
                    self->_deamonHang = false;
                }
                
                // FIXME: 若有需要，可以优化发送方式，将待发送的数据集中到一个buff后再发送
                // 可以在Connection中创建一个100K的buff，数据先写入buff中
                // 若数据写不进则发送buff，然后再将数据进buff
                // 若数据比buff大则发完buff后数据不进buff直接发出去
                // 若当前_waitSendTaskCoList处理完，则将buff发出
                
                // 先发送所有等待发送的RPC请求
                while (!self->_waitSendTaskCoList.empty()) {
                    ClientRpcTask *task = self->_waitSendTaskCoList.front();
                    
                    // 准备发送的数据包头和包体
                    RpcRequestHead head;
                    head.serviceId = task->serviceId;
                    head.methodId = task->methodId;
                    head.callId = uint64_t(task->co);
                    
                    std::string data;
                    task->request->SerializeToString(&data);
                    head.size = data.size();
                    
                    // 放入等待结果Map
                    self->_waitSendTaskCoList.pop_front();
                    self->_waitResultCoMap.insert(std::make_pair(head.callId, task));
                    
                    // 发送包头
                    ret = write(self->_fd, &head, sizeof(head));
                    if (ret <= 0) {
                        printf("Channel::deamonRoutine write head fd %d ret %d errno %d (%s)\n",
                               self->_fd, ret, errno, strerror(errno));
                        
                        self->handleTaskError();
                        break;
                    }
                    assert(ret == sizeof(head));
                    
                    // 发送包体
                    ret = write(self->_fd, data.c_str(), data.size());
                    if (ret <= 0) {
                        printf("Channel::deamonRoutine write body fd %d ret %d errno %d (%s)\n",
                               self->_fd, ret, errno, strerror(errno));
                        
                        self->handleTaskError();
                        break;
                    }
                    assert(ret == data.size());
                }
                
                if (self->_st == CLOSED) {
                    continue;
                }
                
                // 先读头部
                RpcResponseHead head;
                char *head_buf = (char *)(&head);
                int head_len = sizeof(head);
                int total_read_num = 0;
                while (total_read_num < head_len) {
                    ret = read(self->_fd, head_buf + total_read_num, head_len - total_read_num);
                    if (ret <= 0) {
                        // ret 0 mean disconnected
                        if (ret < 0 && errno == EAGAIN) {
                            continue;
                        }
                        
                        // 出错处理
                        printf("Channel::deamonRoutine read head fd %d ret %d errno %d (%s)\n",
                               self->_fd, ret, errno, strerror(errno));
                        
                        self->handleTaskError();
                        break;
                    }
                    
                    total_read_num += ret;
                }
                
                if (self->_st == CLOSED) {
                    continue;
                }
                
                assert(head.size > 0);
                
                // 再读包体
                std::vector<char> buf(head.size);
                total_read_num = 0;
                while (total_read_num < head.size) {
                    ret = read(self->_fd, &buf[0] + total_read_num, head.size - total_read_num);
                    if (ret <= 0) {
                        // ret 0 mean disconnected
                        if (ret < 0 && errno == EAGAIN) {
                            continue;
                        }
                        
                        // 出错处理
                        printf("Channel::deamonRoutine read body fd %d ret %d errno %d (%s)\n",
                               self->_fd, ret, errno, strerror(errno));
                        
                        self->handleTaskError();
                        break;
                    }
                    
                    total_read_num += ret;
                }
                
                if (self->_st == CLOSED) {
                    continue;
                }
                
                // 根据call_id找到对应的协程
                WaitTaskMap::iterator itor = self->_waitResultCoMap.find(head.callId);
                
                if (itor == self->_waitResultCoMap.end()) {
                    // 打印出错信息
                    printf("Channel::deamonRoutine can't find task : %llu\n", head.callId);
                    assert(false);
                    continue;
                }
                
                ClientRpcTask *task = itor->second;
                
                // 解析包体（RpcResponseData）
                if (!task->response->ParseFromArray(&buf[0], head.size)) {
                    errno = EBADMSG;
                } else {
                    errno = 0;
                }
                
                // 唤醒结果对应的等待结果协程进行处理
                self->_waitResultCoMap.erase(itor);
                
                self->_channel->_client->_downQueue.push(task);
            } else {
                // 建立连接
                assert(self->_st == INIT || self->_st == CLOSED);
                assert(self->_fd == -1);
                
                if (self->_st == CLOSED) {
                    // wait a second
                    struct pollfd pf = { 0 };
                    pf.fd = -1;
                    poll( &pf,1,1000);
                }
                
                self->_st = CONNECTING;
                self->_fd = socket(PF_INET, SOCK_STREAM, 0);
                co_set_timeout(self->_fd, -1, 1000);
                printf("co %d socket fd %d\n", co_self(), self->_fd);
                struct sockaddr_in addr;
                
                bzero(&addr,sizeof(addr));
                addr.sin_family = AF_INET;
                addr.sin_port = htons(self->_channel->_port);
                int nIP = 0;
                if (self->_channel->_ip.empty() ||
                    self->_channel->_ip.compare("0") == 0 ||
                    self->_channel->_ip.compare("0.0.0.0") == 0 ||
                    self->_channel->_ip.compare("*") == 0) {
                    nIP = htonl(INADDR_ANY);
                } else {
                    nIP = inet_addr(self->_channel->_ip.c_str());
                }
                
                addr.sin_addr.s_addr = nIP;
                
                ret = connect(self->_fd, (struct sockaddr*)&addr, sizeof(addr));
                
                if ( ret < 0 ) {
                    if ( errno == EALREADY || errno == EINPROGRESS ) {
                        struct pollfd pf = { 0 };
                        pf.fd = self->_fd;
                        pf.events = (POLLOUT|POLLERR|POLLHUP);
                        co_poll( co_get_epoll_ct(),&pf,1,200);
                        //check connect
                        int error = 0;
                        uint32_t socklen = sizeof(error);
                        errno = 0;
                        ret = getsockopt(self->_fd, SOL_SOCKET, SO_ERROR,(void *)&error,  &socklen);
                        if ( ret == -1 ) {
                            // 出错处理
                            printf("Channel::deamonRoutine getsockopt co %d fd %d ret %d errno %d (%s)\n",
                                   co_self(), self->_fd, ret, errno, strerror(errno));
                            
                            close(self->_fd);
                            self->_fd = -1;
                            self->_st = CLOSED;
                            
                            int err = errno;
                            if (!err) {
                                err = ENETUNREACH;
                            }
                            
                            // 唤醒所有等待连接的协程进行错误处理
                            self->wakeUpAll(self->_waitSendTaskCoList, err);
                        } else if ( error ) {
                            // 出错处理
                            printf("Channel::deamonRoutine connect co %d fd %d ret %d errno %d (%s)\n",
                                   co_self(), self->_fd, ret, error, strerror(error));
                            
                            close(self->_fd);
                            self->_fd = -1;
                            self->_st = CLOSED;
                            
                            // 唤醒所有等待连接的协程进行错误处理
                            self->wakeUpAll(self->_waitSendTaskCoList, error);
                        }
                        
                        assert(self->_waitResultCoMap.empty());
                    } else {
                        // 出错处理
                        printf("Channel::deamonRoutine connect co %d fd %d ret %d errno %d (%s)\n",
                               co_self(), self->_fd, ret, errno, strerror(errno));
                        
                        close(self->_fd);
                        self->_fd = -1;
                        self->_st = CLOSED;
                        
                        // 唤醒所有等待连接的协程进行错误处理
                        self->wakeUpAll(self->_waitSendTaskCoList, errno);
                    }
                }
                
                if (self->_st == CLOSED) {
                    continue;
                }
                
                self->_st = CONNECTED;
            }
        }
        
        return NULL;
    }
    
    void Connection::handleTaskError() {
        close(_fd);
        _fd = -1;
        _st = CLOSED;
        
        int err = errno;
        if (!err) {
            err = ENETDOWN;
        }
        
        // 唤醒所有等待结果的协程进行错误处理
        wakeUpAll(_waitResultCoMap, err);
        
        // 唤醒所有等待发送的协程进行错误处理
        wakeUpAll(_waitSendTaskCoList, err);
    }
    
    void Connection::wakeUpAll(WaitTaskList& taskList, int err) {
        while (!taskList.empty()) {
            ClientRpcTask *task = taskList.front();
            taskList.pop_front();
            
            if (err) {
                task->controller->SetFailed(strerror(err));
            }
            
            _channel->_client->_downQueue.push(task);
        }
    }
    
    void Connection::wakeUpAll(WaitTaskMap& taskMap, int err) {
        while (!taskMap.empty()) {
            WaitTaskMap::iterator itor = taskMap.begin();
            ClientRpcTask *task = itor->second;
            taskMap.erase(itor);
            
            if (err) {
                task->controller->SetFailed(strerror(err));
            }
            
            _channel->_client->_downQueue.push(task);
        }
    }
    
    
    Channel::Channel(Client *client, const char* ip, uint32_t port, uint32_t connectNum)
    : _client(client), _ip(ip), _port(port), _conIndex(0) {
        if (connectNum == 0) {
            connectNum = 1;
        }
        
        _connections.resize(connectNum);
        for (int i = 0; i < connectNum; i++) {
            _connections[i] = new Connection(this);
        }
        
        _client->registerChannel(this);
    }
    
    Channel::~Channel() {
        // TODO: 如何优雅的关闭channel？涉及其中connection关闭，而connection中有协程正在执行
        // 一般情况channel是不会被关闭的
    }
    
    void Channel::start(const stCoRoutineAttr_t *attr) {
        for (int i = 0; i < _connections.size(); i++) {
            _connections[i]->start(attr);
        }
    }
    
    Connection *Channel::getNextConnection() {
        _conIndex = (_conIndex + 1) % _connections.size();
        return _connections[_conIndex];
    }
    
    void Channel::CallMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller, const google::protobuf::Message *request, google::protobuf::Message *response, google::protobuf::Closure *done) {
        ClientRpcTask *task = (ClientRpcTask*)calloc( 1,sizeof(ClientRpcTask) );
        task->channel = this;
        task->co = co_self();
        task->request = request;
        task->response = response;
        task->controller = controller;
        //task->serviceId = method->service()->index();
        task->serviceId = method->service()->options().GetExtension(corpc::global_service_id);
        task->methodId = method->index();
        
        _client->_upQueue.push(task);
        
        co_yield_ct();
        
        free(task);
        
        // 正确返回
        if (done) {
            done->Run();
        }
    }
    
}
