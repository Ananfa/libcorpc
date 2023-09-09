/*
 * Created by Xianke Liu on 2017/11/1.
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

#include "corpc_rpc_client.h"
#include "corpc_utils.h"

#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>

#include <google/protobuf/message.h>

#include "corpc_option.pb.h"

using namespace corpc;

void * RpcClient::decode(std::shared_ptr<corpc::Connection> &connection, uint8_t *head, uint8_t *body, int size) {
    std::shared_ptr<Connection> conn = std::static_pointer_cast<Connection>(connection);
    
    uint32_t respSize = *(uint32_t *)head;
    respSize = be32toh(respSize);
    uint64_t callId = *(uint64_t *)(head + 4);
    callId = be64toh(callId);
    uint64_t expireTime = *(uint64_t *)(head + 12);
    expireTime = be64toh(expireTime);
    uint32_t error_code = *(uint32_t *)(head + 20);
    error_code = be32toh(error_code);
    assert(respSize == size);
    std::shared_ptr<ClientTask> task;
    // 注意: _waitResultCoMap需进行线程同步
    {
        LockGuard lock( conn->_waitResultCoMapMutex );
        Connection::WaitTaskMap::iterator itor = conn->_waitResultCoMap.find(callId);
        
        if (itor == conn->_waitResultCoMap.end()) {
            // 注意：RPC超时机制会出现找不到等待结果任务的情况
            return nullptr;
        }

        // 校验RPC任务，由于RPC任务ID是用协程对象的指针值，协程超时机制会让协程返回之前结束，新协程指针值可能与老协程指针相同导致错误唤醒，因此需要通过expireTime校验
        if (itor->second->rpcTask->expireTime != expireTime) {
            return nullptr;
        }
        
        task = std::move(itor->second);
        
        // 唤醒结果对应的等待结果协程进行处理
        conn->_waitResultCoMap.erase(itor);
    }
    
    // 注意：这里操作的是response_1（或controller_1）对象而不是response（或controller）对象是因为有多线程同步问题（有超时时会有多线程同步问题），在调用线程中再通过Swap方法将结果交换到response中
    if (error_code != 0) {
        std::string error_str((char *)body, respSize);
        assert(task->rpcTask->controller_1 == nullptr);

        if (expireTime == 0) {
            task->rpcTask->controller->SetFailed(error_str);
            ((Controller *)task->rpcTask->controller)->SetErrorCode(error_code);
        } else {
            Controller *controller = new Controller();
            controller->SetFailed(error_str);
            controller->SetErrorCode(error_code);
            task->rpcTask->controller_1 = controller;
        }
    } else {
        // 解析包体（RpcResponseData）
        if (!task->rpcTask->response_1->ParseFromArray(body, respSize)) {
            ERROR_LOG("RpcClient::decode -- parse response body fail\n");
            assert(false);
            // 什么情况会导致proto消息解析失败？
            RoutineEnvironment::resumeCoroutine(task->rpcTask->pid, task->rpcTask->co, expireTime, EBADMSG);
            return nullptr;
        }
    }

    // 注意：在这直接进行跨线程协程唤醒，而不是返回后再处理
    RoutineEnvironment::resumeCoroutine(task->rpcTask->pid, task->rpcTask->co, expireTime);

    return nullptr;
}

bool RpcClient::encode(std::shared_ptr<corpc::Connection> &connection, std::shared_ptr<void>& data, uint8_t *buf, int space, int &size, std::string &downflowBuf, uint32_t &downflowBufSentNum) {
    std::shared_ptr<Connection> conn = std::static_pointer_cast<Connection>(connection);
    
    std::shared_ptr<RpcClientTask> rpcTask = std::static_pointer_cast<RpcClientTask>(data);
    int msgSize = rpcTask->request->GetCachedSize();
    if (msgSize == 0) {
        msgSize = rpcTask->request->ByteSizeLong();
    }
    
    // 若空间不足容纳消息头部则等待下次
    if (CORPC_REQUEST_HEAD_SIZE > space) {
        return true;
    }
    
    *(uint32_t *)buf = htobe32(msgSize);
    *(uint32_t *)(buf + 4) = htobe32(rpcTask->serviceId);
    *(uint32_t *)(buf + 8) = htobe32(rpcTask->methodId);
    uint64_t callId = uint64_t(rpcTask->co);
    *(uint64_t *)(buf + 12) = htobe64(callId);
    *(uint64_t *)(buf + 20) = htobe64(rpcTask->expireTime);

    int spaceleft = space - CORPC_REQUEST_HEAD_SIZE;
    if (spaceleft >= msgSize) {
        if (msgSize > 0) {
            rpcTask->request->SerializeWithCachedSizesToArray(buf + CORPC_REQUEST_HEAD_SIZE);
        }
        
        size = CORPC_REQUEST_HEAD_SIZE + msgSize;
    } else {
        downflowBuf.assign(msgSize, 0);
        uint8_t *dbuf = (uint8_t*)downflowBuf.data();
        rpcTask->request->SerializeWithCachedSizesToArray(dbuf);
        
        if (spaceleft > 0) {
            memcpy(buf + CORPC_REQUEST_HEAD_SIZE, dbuf, spaceleft);
            downflowBufSentNum = spaceleft;
        }
        
        size = space;
    }
    
    if (!rpcTask->response) {
        // 对于not_care_response类型的rpc调用，这里需要触发回调来清理request对象
        assert(rpcTask->done);
        rpcTask->done->Run();
    }
    
    return true;
}

RpcClient::Connection::Connection(std::shared_ptr<ChannelCore> channel): corpc::Connection(-1, channel->_client->_io, false), _channel(channel), _st(CLOSED) {
}

void RpcClient::Connection::onClose() {
    ConnectionTask *connectionTask = new ConnectionTask;
    connectionTask->type = ConnectionTask::CLOSE;
    connectionTask->connection = std::static_pointer_cast<Connection>(corpc::Connection::shared_from_this());
    
    _channel->_client->_connectionTaskQueue.push(connectionTask);
}

void RpcClient::Connection::cleanDataOnClosing(std::shared_ptr<void>& data) {
    std::shared_ptr<RpcClientTask> rpcTask = std::static_pointer_cast<RpcClientTask>(data);
    
    // not_care_response类型rpc调用需要通过回调清理request
    if (!rpcTask->response) {
        if (rpcTask->controller != NULL) {
            rpcTask->controller->SetFailed(strerror(ENETDOWN));
        }
        
        rpcTask->done->Run();
    }
}

RpcClient::ChannelCore::ChannelCore(RpcClient *client, const std::string& host, uint32_t port, uint32_t connectNum)
: _client(client), _host(host), _port(port), _conIndex(0), _connectDelay(false) {
    if (connectNum == 0) {
        connectNum = 1;
    }
    
    for (int i = 0; i < connectNum; i++) {
        _connections.push_back(nullptr);
    }
}

RpcClient::ChannelCore::~ChannelCore() {
    // TODO: 如何优雅的关闭channel？涉及其中connection关闭，而connection中有协程正在执行
    // 一般情况channel是不会被关闭的
}

std::shared_ptr<RpcClient::Connection> RpcClient::ChannelCore::getNextConnection() {
    // 若Channel正在清理，连接会被清空，此时返回空
    if (!_connections.size()) {
        return nullptr;
    }
    
    _conIndex = (_conIndex + 1) % _connections.size();
    
    if (_connections[_conIndex] == nullptr || _connections[_conIndex]->_st == Connection::CLOSED) {
        _connections[_conIndex] = std::make_shared<Connection>(shared_from_this());
        
        std::shared_ptr<corpc::Connection> connection = _connections[_conIndex];
        
        std::shared_ptr<corpc::Pipeline> pipeline = _client->_pipelineFactory->buildPipeline(connection);
        connection->setPipeline(pipeline);
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

void RpcClient::ChannelCore::CallMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller, const google::protobuf::Message *request, google::protobuf::Message *response, google::protobuf::Closure *done) {
    std::shared_ptr<ClientTask> clientTask(new ClientTask);
    clientTask->channel = shared_from_this();
    clientTask->rpcTask = std::make_shared<RpcClientTask>();
    clientTask->rpcTask->pid = GetPid();
    clientTask->rpcTask->co = co_self();
    clientTask->rpcTask->request = request;
    clientTask->rpcTask->request_1 = NULL; // 跨进程RPC调用不需要拷贝request
    clientTask->rpcTask->controller = controller;
    clientTask->rpcTask->controller_1 = NULL;
    clientTask->rpcTask->done = done;
    clientTask->rpcTask->serviceId = method->service()->options().GetExtension(corpc::global_service_id);
    clientTask->rpcTask->methodId = method->index();

    bool care_response = !method->options().GetExtension(corpc::not_care_response);
    uint32_t timeout = method->options().GetExtension(corpc::timeout);
    assert(care_response || done); // not_care_response need done
    if (care_response) {
        assert(response != NULL);
        clientTask->rpcTask->response = response;
        if (timeout > 0) {
            clientTask->rpcTask->response_1 = response->New();
            struct timeval t;
            gettimeofday(&t, NULL);
            uint64_t now = t.tv_sec * 1000 + t.tv_usec / 1000;
            clientTask->rpcTask->expireTime = now + timeout;
            RoutineEnvironment::addTimeoutTask(clientTask->rpcTask);
        } else {
            clientTask->rpcTask->response_1 = response;
            clientTask->rpcTask->expireTime = 0;
        }
    } else {
        assert(timeout == 0);
        clientTask->rpcTask->response = NULL;
        clientTask->rpcTask->response_1 = NULL;
        clientTask->rpcTask->expireTime = 0;
    }

    _client->_taskQueue.push(std::move(clientTask));

    if (care_response) {
        co_yield_ct(); // 等待rpc结果到来被唤醒继续执行
        
        // 正确返回
        if (done) {
            done->Run();
        }
    }
    
}

RpcClient::Channel::Guard::~Guard() {
    // 发消息给RpcClient线程来清理Channel
    // 注意：不能在这里清理，因为会产生多线程问题
    _channel->_client->_clearChannelQueue.push(_channel);
}

RpcClient::RpcClient(IO *io): _io(io) {
    _pipelineFactory = new TcpPipelineFactory(NULL, decode, encode, CORPC_RESPONSE_HEAD_SIZE, CORPC_MAX_RESPONSE_SIZE, 0, corpc::MessagePipeline::FOUR_BYTES);
}

RpcClient* RpcClient::create(IO *io) {
    assert(io);
    RpcClient *client = new RpcClient(io);
    
    client->start();
    return client;
}

void RpcClient::start() {
    _t = std::thread(threadEntry, this);
}

void RpcClient::threadEntry(RpcClient *self) {
    RoutineEnvironment::startCoroutine(connectionRoutine, self);
    
    RoutineEnvironment::startCoroutine(taskHandleRoutine, self);

    RoutineEnvironment::startCoroutine(clearChannelRoutine, self);
    
    RoutineEnvironment::runEventLoop();
}

void *RpcClient::connectionRoutine( void * arg ) {
    RpcClient *self = (RpcClient *)arg;
    
    int readFd = self->_connectionTaskQueue.getReadFd();
    co_register_fd(readFd);
    co_set_timeout(readFd, -1, 1000);
    
    IO *io = self->_io;
    Sender *sender = io->getSender();
    
    int ret;
    std::vector<char> buf(1024);
    while (true) {
        // 等待处理信号
        ret = (int)read(readFd, &buf[0], 1024);
        assert(ret != 0);
        if (ret < 0) {
            if (errno == EAGAIN) {
                continue;
            } else {
                // 管道出错
                ERROR_LOG("RpcClient::connectionRoutine read from up pipe fd %d ret %d errno %d (%s)\n",
                       readFd, ret, errno, strerror(errno));
                
                // TODO: 如何处理？退出协程？
                // sleep 10 milisecond
                msleep(10);
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
                        LockGuard lock( connection->_waitResultCoMapMutex );
                        while (!connection->_waitResultCoMap.empty()) {
                            Connection::WaitTaskMap::iterator itor = connection->_waitResultCoMap.begin();
                            std::shared_ptr<ClientTask> task = std::move(itor->second);
                            connection->_waitResultCoMap.erase(itor);
                            
                            if (!task->rpcTask->expireTime) {
                                task->rpcTask->controller->SetFailed(strerror(ENETDOWN));
                            }
                            
                            RoutineEnvironment::resumeCoroutine(task->rpcTask->pid, task->rpcTask->co, task->rpcTask->expireTime, ENETDOWN);
                        }
                    }
                    
                    // 注意：连接断开时，需要调用回调
                    std::list<std::shared_ptr<void>>& datas = connection->_datas;
                    for (auto& data : datas) {
                        std::shared_ptr<RpcClientTask> rpcTask = std::static_pointer_cast<RpcClientTask>(data);
                        
                        // 对于not_care_response类型的rpc需要在这里调用回调处理
                        if (!rpcTask->response) {
                            if (rpcTask->controller) {
                                rpcTask->controller->SetFailed(strerror(ENETDOWN));
                            }
                            
                            rpcTask->done->Run();
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
                    LOG("co %d socket fd %d\n", co_self(), connection->_fd);
                    struct sockaddr_in addr;
                    
                    std::shared_ptr<ChannelCore>& channel = connection->_channel;
                    bzero(&addr,sizeof(addr));
                    addr.sin_family = AF_INET;
                    addr.sin_port = htons(channel->_port);
                    int nIP = 0;
                    if (channel->_host.empty() ||
                        channel->_host.compare("0") == 0 ||
                        channel->_host.compare("0.0.0.0") == 0 ||
                        channel->_host.compare("*") == 0) {
                        nIP = htonl(INADDR_ANY);
                    } else {
                        nIP = inet_addr(channel->_host.c_str());
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
                                ERROR_LOG("RpcClient::connectRoutine getsockopt co %d fd %d ret %d errno %d (%s)\n",
                                       co_self(), connection->_fd, ret, errno, strerror(errno));
                                
                                close(connection->_fd);
                                connection->_fd = -1;
                                connection->_st = Connection::CLOSED;
                            } else if ( error ) {
                                // 出错处理
                                ERROR_LOG("RpcClient::connectRoutine getsockopt co %d fd %d ret %d errno %d (%s)\n",
                                       co_self(), connection->_fd, ret, error, strerror(error));
                                
                                close(connection->_fd);
                                connection->_fd = -1;
                                connection->_st = Connection::CLOSED;
                            }
                            
                            assert(connection->_waitResultCoMap.empty());
                        } else {
                            // 出错处理
                            ERROR_LOG("RpcClient::connectRoutine connect co %d fd %d ret %d errno %d (%s)\n",
                                   co_self(), connection->_fd, ret, errno, strerror(errno));
                            
                            close(connection->_fd);
                            connection->_fd = -1;
                            connection->_st = Connection::CLOSED;
                        }
                    }
                    
                    if (connection->_st == Connection::CLOSED) {
                        // 连接失败，唤醒所有等待连接建立的rpc调用协程进行错误处理
                        while (!connection->_waitSendTaskCoList.empty()) {
                            std::shared_ptr<ClientTask> task = std::move(connection->_waitSendTaskCoList.front());
                            connection->_waitSendTaskCoList.pop_front();
                            
                            if (task->rpcTask->response) {
                                if (!task->rpcTask->expireTime) {
                                    task->rpcTask->controller->SetFailed(strerror(errno));
                                }

                                RoutineEnvironment::resumeCoroutine(task->rpcTask->pid, task->rpcTask->co, task->rpcTask->expireTime, errno);
                            } else {
                                if (task->rpcTask->controller) {
                                    task->rpcTask->controller->SetFailed(strerror(errno));
                                }

                                // not_care_response类型的rpc需要在这里触发回调清理request
                                assert(task->rpcTask->done);
                                task->rpcTask->done->Run();
                            }
                        }
                        
                        assert(connection->_waitResultCoMap.empty());
                        break;
                    }
                    
                    connection->_st = Connection::CONNECTED;
                    connection->_channel->_connectDelay = false;
                    
                    setKeepAlive(connection->_fd, 10);
                    // 加入到IO中
                    std::shared_ptr<corpc::Connection> ioConnection = std::static_pointer_cast<corpc::Connection>(connection);
                    io->addConnection(ioConnection);
                    
                    struct timeval t;
                    gettimeofday(&t, NULL);
                    uint64_t now = t.tv_sec * 1000 + t.tv_usec / 1000; // 当前时间（毫秒精度）
                    // 发送等待发送队列中的任务
                    while (!connection->_waitSendTaskCoList.empty()) {
                        std::shared_ptr<ClientTask> task = std::move(connection->_waitSendTaskCoList.front());
                        connection->_waitSendTaskCoList.pop_front();
                        
                        //std::shared_ptr<corpc::Connection> ioConn = std::static_pointer_cast<corpc::Connection>(connection);

                        if (task->rpcTask->response) {
                            // 若rpc任务已超时就不需发给服务器
                            if (task->rpcTask->expireTime == 0 || now < task->rpcTask->expireTime) {
                                {
                                    LockGuard lock(connection->_waitResultCoMapMutex);

                                    // 注意：由于加入RPC超时机制后，rpc请求协程会在处理超时时结束请求，但_waitResultCoMap中还留有旧记录，新rpc请求会insert不了，改为赋值替换
                                    //connection->_waitResultCoMap.insert(std::make_pair(uint64_t(task->rpcTask->co), task));
                                    connection->_waitResultCoMap[uint64_t(task->rpcTask->co)] = task;
                                }

                                sender->send(ioConnection, task->rpcTask);
                            }
                        } else {
                            sender->send(ioConnection, task->rpcTask);
                        }
                    }
                    
                    break;
                }
            }
            
            delete task;
            
            task = self->_connectionTaskQueue.pop();
        }
        
    }

    return NULL;
}

void *RpcClient::taskHandleRoutine(void *arg) {
    RpcClient *self = (RpcClient *)arg;
    
    ClientTaskQueue& queue = self->_taskQueue;
    Sender *sender = self->_io->getSender();
    
    // 初始化pipe readfd
    int readFd = queue.getReadFd();
    co_register_fd(readFd);
    co_set_timeout(readFd, -1, 1000);
    
    int ret;
    std::vector<char> buf(1024);
    while (true) {
        // 等待处理信号
        ret = (int)read(readFd, &buf[0], 1024);
        assert(ret != 0);
        if (ret < 0) {
            if (errno == EAGAIN) {
                continue;
            } else {
                // 管道出错
                ERROR_LOG("RpcClient::taskHandleRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                       readFd, ret, errno, strerror(errno));
                
                // TODO: 如何处理？退出协程？
                // sleep 10 milisecond
                msleep(10);
            }
        }
        
        struct timeval t1,t2;
        gettimeofday(&t1, NULL);
        uint64_t now = t1.tv_sec * 1000 + t1.tv_usec / 1000; // 当前时间（毫秒精度）
        int count = 0;
        
        // 处理任务队列
        std::shared_ptr<ClientTask> task = queue.pop();
        while (task) {
            // 先从channel中获得一connection
            std::shared_ptr<Connection> conn = task->channel->getNextConnection();
            assert(conn->_st != Connection::CLOSED);
            
            if (!conn) {
                // 若连接为空，需要应直接唤醒rpc任务对应的协程进行出错处理。注意：not_care_response类型的请求不需要唤醒
                if (task->rpcTask->response) {
                    if (!task->rpcTask->expireTime) {
                        task->rpcTask->controller->SetFailed(strerror(ENETDOWN));
                    }
                    
                    RoutineEnvironment::resumeCoroutine(task->rpcTask->pid, task->rpcTask->co, task->rpcTask->expireTime, ENETDOWN);
                } else {
                    if (task->rpcTask->controller) {
                        task->rpcTask->controller->SetFailed(strerror(ENETDOWN));
                    }

                    // not_care_response类型的rpc需要在这里触发回调清理request
                    assert(task->rpcTask->done);
                    task->rpcTask->done->Run();
                }
            } else {
                // 当连接未建立时，放入等待发送队列，等连接建立好时再发送rpc请求，若连接不成功，需要将等待中的rpc请求都进行出错处理（唤醒其协程）
                // 由于upRoutine和connectRoutine在同一线程中，因此放入等待发送队列不需要进行锁同步
                if (conn->_st == Connection::CONNECTING) {
                    conn->_waitSendTaskCoList.push_back(task);
                } else {
                    std::shared_ptr<corpc::Connection> ioConn = std::static_pointer_cast<corpc::Connection>(conn);
                    
                    if (task->rpcTask->response) {
                        // 若rpc任务已超时就不需发给服务器
                        if (task->rpcTask->expireTime == 0 || now < task->rpcTask->expireTime) {
                            {
                                LockGuard lock(conn->_waitResultCoMapMutex);

                                // 注意：由于加入RPC超时机制后，rpc请求协程会在处理超时时结束请求，但_waitResultCoMap中还留有旧记录，新rpc请求会insert不了，改为赋值替换
                                //conn->_waitResultCoMap.insert(std::make_pair(uint64_t(task->rpcTask->co), task));
                                conn->_waitResultCoMap[uint64_t(task->rpcTask->co)] = task;
                            }
                        
                            sender->send(ioConn, task->rpcTask);
                        }
                    } else {
                        sender->send(ioConn, task->rpcTask);
                    }
                }
            }
            
            // 防止其他协程（如：RoutineEnvironment::cleanRoutine）长时间不被调度，让其他协程处理一下
            count++;
            if (count == 100) {
                gettimeofday(&t2, NULL);
                if ((t2.tv_sec - t1.tv_sec) * 1000000 + t2.tv_usec - t1.tv_usec > 100000) {
                    RoutineEnvironment::pause();
                    
                    gettimeofday(&t1, NULL);
                    now = t1.tv_sec * 1000 + t1.tv_usec / 1000;
                }
                count = 0;
            }
            
            task = queue.pop();
        }
    }
    
    return NULL;
}

void *RpcClient::clearChannelRoutine(void *arg) {
    RpcClient *self = (RpcClient *)arg;
    
    ClearChannelQueue& queue = self->_clearChannelQueue;

    // 初始化pipe readfd
    int readFd = queue.getReadFd();
    co_register_fd(readFd);
    co_set_timeout(readFd, -1, 1000);
    
    int ret;
    std::vector<char> buf(1024);
    while (true) {
        // 等待处理信号
        ret = (int)read(readFd, &buf[0], 1024);
        assert(ret != 0);
        if (ret < 0) {
            if (errno == EAGAIN) {
                continue;
            } else {
                // 管道出错
                ERROR_LOG("RpcClient::clearChannelRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                          readFd, ret, errno, strerror(errno));
                
                // TODO: 如何处理？退出协程？
                // sleep 10 milisecond
                msleep(10);
            }
        }
        
        // 处理任务队列
        std::shared_ptr<ChannelCore> channel = queue.pop();
        while (channel) {
            for (auto& connection : channel->_connections) {
                if (connection) {
                    connection->close();
                }
            }
            
            channel->_connections.clear();
            
            channel = queue.pop();
        }
    }
    
    return NULL;
}