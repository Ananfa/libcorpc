//
//  co_rpc_client.cpp
//  rpccli
//
//  Created by Xianke Liu on 2017/11/1.
//  Copyright © 2017年 Dena. All rights reserved.
//
#include "corpc_routine_env.h"

#include "corpc_client.h"

#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>

#include <google/protobuf/message.h>

#include "corpc_option.pb.h"

namespace CoRpc {
    
    Client::Connection::Connection(Channel *channel): IO::Connection(-1, channel->_client->_io), _channel(channel), _st(CLOSED), _respdata(CORPC_MAX_RESPONSE_SIZE,0), _headNum(0), _dataNum(0) {
        _head_buf = (char *)(&_resphead);
        _data_buf = (uint8_t *)_respdata.data();
    }
    
    bool Client::Connection::parseData(uint8_t *buf, int size) {
        // 解析数据
        int offset = 0;
        while (size > offset) {
            // 先解析头部
            if (_headNum < sizeof(RpcResponseHead)) {
                int needNum = sizeof(RpcResponseHead) - _headNum;
                if (size - offset > needNum) {
                    memcpy(_head_buf + _headNum, buf + offset, needNum);
                    _headNum = sizeof(RpcResponseHead);
                    
                    offset += needNum;
                } else {
                    memcpy(_head_buf + _headNum, buf + offset, size - offset);
                    _headNum += size - offset;
                    
                    break;
                }
            }
            
            if (_resphead.size > CORPC_MAX_RESPONSE_SIZE) { // 数据超长
                printf("ERROR: Client::Connection::parseData -- response too large for fd:%d in thread:%d\n", _fd, GetPid());
                
                return false;
            }
            
            // 从缓存中解析数据
            if (_dataNum < _resphead.size) {
                int needNum = _resphead.size - _dataNum;
                if (size - offset >= needNum) {
                    memcpy(_data_buf + _dataNum, buf + offset, needNum);
                    _dataNum = _resphead.size;
                    
                    offset += needNum;
                } else {
                    memcpy(_data_buf + _dataNum, buf + offset, size - offset);
                    _dataNum += size - offset;
                    
                    break;
                }
            }
            
            ClientTask *task = NULL;
            // 注意: _waitResultCoMap需进行线程同步
            {
                std::unique_lock<std::mutex> lock( _waitResultCoMapMutex );
                WaitTaskMap::iterator itor = _waitResultCoMap.find(_resphead.callId);
                
                if (itor == _waitResultCoMap.end()) {
                    // 打印出错信息
                    printf("Client::Connection::parseData can't find task : %llu\n", _resphead.callId);
                    assert(false);
                    return false;
                }
                
                task = itor->second;
                
                // 唤醒结果对应的等待结果协程进行处理
                _waitResultCoMap.erase(itor);
            }
            
            // 解析包体（RpcResponseData）
            if (!task->rpcTask->response->ParseFromArray(_data_buf, _resphead.size)) {
                printf("ERROR: Client::Connection::parseData -- parse response body fail\n");
                return false;
            }
            
            _channel->_client->_downQueue.push(task->rpcTask->co);
            
            // 处理完一个请求消息，复位状态
            _headNum = 0;
            _dataNum = 0;
        }
        
        return true;
    }
    
    int Client::Connection::buildData(uint8_t *buf, int space) {
        int used = 0;
        while (_datas.size() > 0) {
            std::shared_ptr<RpcTask> rpcTask = std::static_pointer_cast<RpcTask>(_datas.front());
            int msgSize = rpcTask->request->GetCachedSize();
            if (msgSize == 0) {
                msgSize = rpcTask->request->ByteSize();
            }
            
            if (msgSize + sizeof(RpcRequestHead) >= space - used) {
                break;
            }
            
            // head
            RpcRequestHead reqhead;
            reqhead.serviceId = rpcTask->serviceId;
            reqhead.methodId = rpcTask->methodId;
            reqhead.callId = uint64_t(rpcTask->co);
            reqhead.size = msgSize;
            
            memcpy(buf + used, &reqhead, sizeof(RpcRequestHead));
            used += sizeof(RpcRequestHead);
            
            rpcTask->request->SerializeWithCachedSizesToArray(buf + used);
            used += msgSize;
            
            _datas.pop_front();
        }
        
        return used;
    }
    
    void Client::Connection::onClose() {
        ConnectionTask *connectionTask = new ConnectionTask;
        connectionTask->type = ConnectionTask::CLOSE;
        connectionTask->connection = std::static_pointer_cast<Connection>(IO::Connection::shared_from_this());
        
        _channel->_client->_connectionTaskQueue.push(connectionTask);
    }
    
    Client::Channel::Channel(Client *client, const char* ip, uint32_t port, uint32_t connectNum)
    : _client(client), _ip(ip), _port(port), _conIndex(0), _connectDelay(false) {
        if (connectNum == 0) {
            connectNum = 1;
        }
        
        //_connections.resize(connectNum, nullptr);
        for (int i = 0; i < connectNum; i++) {
            _connections.push_back(nullptr);
        }
        
        _client->registerChannel(this);
    }
    
    Client::Channel::~Channel() {
        // TODO: 如何优雅的关闭channel？涉及其中connection关闭，而connection中有协程正在执行
        // 一般情况channel是不会被关闭的
    }
    
    std::shared_ptr<Client::Connection>& Client::Channel::getNextConnection() {
        _conIndex = (_conIndex + 1) % _connections.size();
        
        if (_connections[_conIndex] == nullptr || _connections[_conIndex]->_st == Connection::CLOSED) {
            _connections[_conIndex] = std::make_shared<Connection>(this);
        }
        
        if (_connections[_conIndex]->_st == Connection::CLOSED) {
            _connections[_conIndex]->_st = Connection::CONNECTING;
            
            ConnectionTask *connectionTask = new ConnectionTask;
            connectionTask->type = ConnectionTask::CONNECT;
            connectionTask->connection = _connections[_conIndex];
            
            _client->_connectionTaskQueue.push(connectionTask);
        }
        
        return _connections[_conIndex];
    }
    
    void Client::Channel::CallMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller, const google::protobuf::Message *request, google::protobuf::Message *response, google::protobuf::Closure *done) {
        ClientTask *clientTask = new ClientTask;
        clientTask->channel = this;
        clientTask->rpcTask = std::make_shared<RpcTask>();
        clientTask->rpcTask->co = co_self();
        clientTask->rpcTask->request = request;
        clientTask->rpcTask->response = response;
        clientTask->rpcTask->controller = controller;
        clientTask->rpcTask->serviceId = method->service()->options().GetExtension(corpc::global_service_id);
        clientTask->rpcTask->methodId = method->index();
        
        _client->_upList.push_back(clientTask);
        if (_client->_upRoutineHang) {
            co_resume(_client->_upRoutine);
        }
        
        co_yield_ct(); // 等待rpc结果到来被唤醒继续执行
        
        delete clientTask;
        
        // 正确返回
        if (done) {
            done->Run();
        }
    }
    
    bool Client::registerChannel(Channel *channel) {
        if (_channelSet.find(channel) != _channelSet.end()) {
            return false;
        }
        
        _channelSet.insert(std::make_pair(channel, channel));
        return true;
    }
    
    void Client::start() {
        RoutineEnvironment::startCoroutine(connectionRoutine, this);
        RoutineEnvironment::startCoroutine(downRoutine, this);
        RoutineEnvironment::startCoroutine(upRoutine, this);
    }
    
    void *Client::connectionRoutine( void * arg ) {
        Client *self = (Client *)arg;
        co_enable_hook_sys();
        
        int readFd = self->_connectionTaskQueue.getReadFd();
        co_register_fd(readFd);
        co_set_timeout(readFd, -1, 1000);
        
        int ret;
        std::vector<char> buf(1024);
        while (true) {
            // 等待处理信号
            ret = read(readFd, &buf[0], 1024);
            assert(ret != 0);
            if (ret < 0) {
                if (errno == EAGAIN) {
                    continue;
                } else {
                    // 管道出错
                    printf("Error: Client::connectionRoutine read from up pipe fd %d ret %d errno %d (%s)\n",
                           readFd, ret, errno, strerror(errno));
                    
                    // TODO: 如何处理？退出协程？
                    // sleep 10 milisecond
                    usleep(10000);
                }
            }
            
            ConnectionTask *task = self->_connectionTaskQueue.pop();
            while (task) {
                std::shared_ptr<Connection> connection = task->connection;
                
                switch (task->type) {
                    case ConnectionTask::CLOSE: {
                        // 清理
                        connection->_st = Connection::CLOSED;
                        connection->_channel->_connectDelay = true;
                        
                        assert(connection->_waitSendTaskCoList.empty());
                        
                        {
                            std::unique_lock<std::mutex> lock( connection->_waitResultCoMapMutex );
                            while (!connection->_waitResultCoMap.empty()) {
                                Connection::WaitTaskMap::iterator itor = connection->_waitResultCoMap.begin();
                                ClientTask *task = itor->second;
                                connection->_waitResultCoMap.erase(itor);
                                
                                task->rpcTask->controller->SetFailed(strerror(ENETDOWN));
                                
                                self->_downQueue.push(task->rpcTask->co);
                            }
                        }
                        
                        break;
                    }
                    case ConnectionTask::CONNECT: {
                        assert(connection->_st == Connection::CONNECTING);
                        if (connection->_channel->_connectDelay) {
                            // sleep 1 second
                            sleep(1);
                        }
                        
                        // 建立连接
                        connection->_fd = socket(PF_INET, SOCK_STREAM, 0);
                        co_set_timeout(connection->_fd, -1, 1000);
                        printf("co %d socket fd %d\n", co_self(), connection->_fd);
                        struct sockaddr_in addr;
                        
                        Channel *channel = connection->_channel;
                        bzero(&addr,sizeof(addr));
                        addr.sin_family = AF_INET;
                        addr.sin_port = htons(channel->_port);
                        int nIP = 0;
                        if (channel->_ip.empty() ||
                            channel->_ip.compare("0") == 0 ||
                            channel->_ip.compare("0.0.0.0") == 0 ||
                            channel->_ip.compare("*") == 0) {
                            nIP = htonl(INADDR_ANY);
                        } else {
                            nIP = inet_addr(channel->_ip.c_str());
                        }
                        
                        addr.sin_addr.s_addr = nIP;
                        
                        int ret = connect(connection->_fd, (struct sockaddr*)&addr, sizeof(addr));
                        
                        if ( ret < 0 ) {
                            if ( errno == EALREADY || errno == EINPROGRESS ) {
                                struct pollfd pf = { 0 };
                                pf.fd = connection->_fd;
                                pf.events = (POLLOUT|POLLERR|POLLHUP);
                                co_poll( co_get_epoll_ct(),&pf,1,200);
                                //check connect
                                int error = 0;
                                uint32_t socklen = sizeof(error);
                                errno = 0;
                                ret = getsockopt(connection->_fd, SOL_SOCKET, SO_ERROR,(void *)&error,  &socklen);
                                if ( ret == -1 ) {
                                    // 出错处理
                                    printf("Client::connectRoutine getsockopt co %d fd %d ret %d errno %d (%s)\n",
                                           co_self(), connection->_fd, ret, errno, strerror(errno));
                                    
                                    close(connection->_fd);
                                    connection->_fd = -1;
                                    connection->_st = Connection::CLOSED;
                                } else if ( error ) {
                                    // 出错处理
                                    printf("Client::connectRoutine getsockopt co %d fd %d ret %d errno %d (%s)\n",
                                           co_self(), connection->_fd, ret, error, strerror(error));
                                    
                                    close(connection->_fd);
                                    connection->_fd = -1;
                                    connection->_st = Connection::CLOSED;
                                }
                                
                                assert(connection->_waitResultCoMap.empty());
                            } else {
                                // 出错处理
                                printf("Client::connectRoutine connect co %d fd %d ret %d errno %d (%s)\n",
                                       co_self(), connection->_fd, ret, errno, strerror(errno));
                                
                                close(connection->_fd);
                                connection->_fd = -1;
                                connection->_st = Connection::CLOSED;
                            }
                        }
                        
                        if (connection->_st == Connection::CLOSED) {
                            // 唤醒所有等待连接的协程进行错误处理
                            while (!connection->_waitSendTaskCoList.empty()) {
                                ClientTask *task = connection->_waitSendTaskCoList.front();
                                connection->_waitSendTaskCoList.pop_front();
                                
                                task->rpcTask->controller->SetFailed("Connect fail");
                                
                                self->_downQueue.push(task->rpcTask->co);
                            }
                            
                            assert(connection->_waitResultCoMap.empty());
                            break;
                        }
                        
                        connection->_st = Connection::CONNECTED;
                        connection->_channel->_connectDelay = false;
                        
                        // 加入到IO中
                        std::shared_ptr<IO::Connection> ioConnection = std::static_pointer_cast<IO::Connection>(connection);
                        self->_io->addConnection(ioConnection);
                        
                        // 发送等待发送队列中的任务
                        while (!connection->_waitSendTaskCoList.empty()) {
                            ClientTask *task = connection->_waitSendTaskCoList.front();
                            connection->_waitSendTaskCoList.pop_front();
                            
                            std::shared_ptr<IO::Connection> ioConn = std::static_pointer_cast<IO::Connection>(connection);
                            
                            {
                                std::unique_lock<std::mutex> lock(connection->_waitResultCoMapMutex);
                                connection->_waitResultCoMap.insert(std::make_pair(uint64_t(task->rpcTask->co), task));
                            }
                            
                            self->_io->getSender()->send(ioConn, task->rpcTask);
                        }
                        
                        break;
                    }
                }
                
                delete task;
                
                task = self->_connectionTaskQueue.pop();
            }
            
        }
    }
    
    void *Client::upRoutine( void * arg ) {
        Client *self = (Client *)arg;
        co_enable_hook_sys();
        
        self->_upRoutine = co_self();
        self->_upRoutineHang = false;
        
        while (true) {
            if (self->_upList.empty()) {
                // 挂起
                self->_upRoutineHang = true;
                co_yield_ct();
                self->_upRoutineHang = false;
                
                continue;
            }
            
            // 处理任务队列
            ClientTask *task = self->_upList.front();
            self->_upList.pop_front();
            assert(task);
            
            // 先从channel中获得一connection
            std::shared_ptr<Connection> conn = ((Channel*)(task->channel))->getNextConnection();
            assert(conn->_st != Connection::CLOSED);

            // 当连接未建立时，放入等待发送队列，等连接建立好时再发送rpc请求，若连接不成功，需要将等待中的rpc请求都进行出错处理（唤醒其协程）
            // 由于upRoutine和connectRoutine在同一线程中，因此放入等待发送队列不需要进行锁同步
            if (conn->_st == Connection::CONNECTING) {
                conn->_waitSendTaskCoList.push_back(task);
            } else {
                std::shared_ptr<IO::Connection> ioConn = std::static_pointer_cast<IO::Connection>(conn);
                
                {
                    std::unique_lock<std::mutex> lock(conn->_waitResultCoMapMutex);
                    conn->_waitResultCoMap.insert(std::make_pair(uint64_t(task->rpcTask->co), task));
                }
                
                self->_io->getSender()->send(ioConn, task->rpcTask);
            }
        }
        
        return NULL;
    }
    
    void *Client::downRoutine( void * arg ) {
        Client *self = (Client *)arg;
        co_enable_hook_sys();
        int readFd = self->_downQueue.getReadFd();
        co_register_fd(readFd);
        co_set_timeout(readFd, -1, 1000);
        
        int ret;
        std::vector<char> buf(1024);
        while (true) {
            // 等待处理信号
            ret = read(readFd, &buf[0], 1024);
            assert(ret != 0);
            if (ret < 0) {
                if (errno == EAGAIN) {
                    continue;
                } else {
                    // 管道出错
                    printf("Error: Client::downQueueRoutine read from up pipe fd %d ret %d errno %d (%s)\n",
                           readFd, ret, errno, strerror(errno));
                    
                    // TODO: 如何处理？退出协程？
                    // sleep 10 milisecond
                    usleep(10000);
                }
            }
            
            // 处理任务队列
            stCoRoutine_t *co = self->_downQueue.pop();
            while (co) {
                co_resume(co);
                
                co = self->_downQueue.pop();
            }
        }
        
        return NULL;
    }
}
