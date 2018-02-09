//
//  co_rpc_server.cpp
//  rpcsvr
//
//  Created by Xianke Liu on 2017/11/21.
//  Copyright © 2017年 Dena. All rights reserved.
//
#include "co_rpc_routine_env.h"

#include "co_rpc_server.h"

#include "co_rpc_controller.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <sys/time.h>

#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "corpc_option.pb.h"

// TODO: 使用统一的Log接口记录Log

namespace CoRpc {
    Server::Server(bool acceptInNewThread, uint16_t receiveThreadNum, uint16_t workThreadNum, const std::string& ip, uint16_t port): _acceptInNewThread(acceptInNewThread), _receiveThreadNum(receiveThreadNum), _workThreadNum(workThreadNum), _ip(ip), _port(port) {
    }
    
    bool Server::registerService(::google::protobuf::Service *rpcService) {
        const google::protobuf::ServiceDescriptor *serviceDescriptor = rpcService->GetDescriptor();
        
        uint32_t serviceId = (uint32_t)(serviceDescriptor->options().GetExtension(corpc::global_service_id));
        
        //uint64_t serviceId = serviceDescriptor->index();
        std::map<uint32_t, ServiceData>::iterator it = _services.find(serviceId);
        if (it != _services.end()) {
            return false;
        }
        
        ServiceData &serviceData = _services[serviceId];
        serviceData.rpcService = rpcService;
        
        MethodData methodData;
        for (int i = 0; i < serviceDescriptor->method_count(); i++) {
            methodData._method_descriptor = serviceDescriptor->method(i);
            methodData._request_proto = &rpcService->GetRequestPrototype(methodData._method_descriptor);
            methodData._response_proto= &rpcService->GetResponsePrototype(methodData._method_descriptor);
            
            serviceData.methods.push_back(methodData);
        }
        
        return true;
    }
    
    google::protobuf::Service *Server::getService(uint32_t serviceId) const {
        std::map<uint32_t, ServiceData>::const_iterator it = _services.find(serviceId);
        if (it == _services.end()) {
            return NULL;
        }
        
        return it->second.rpcService;
    }
    
    const MethodData *Server::getMethod(uint32_t serviceId, uint32_t methodId) const {
        std::map<uint32_t, ServiceData>::const_iterator it = _services.find(serviceId);
        if (it == _services.end()) {
            return NULL;
        }
        
        if (it->second.methods.size() <= methodId) {
            return NULL;
        }
        
        return &(it->second.methods[methodId]);
    }

    bool Server::start() {
        // 根据需要启动accept协程或线程
        if (_acceptInNewThread) {
            _acceptor = new ThreadAcceptor(this);
        } else {
            _acceptor = new CoroutineAcceptor(this);
        }
        
        // 根据需要启动连接处理线程或协程
        if (_receiveThreadNum > 0) {
            _receiver = new MultiThreadReceiver(this, _receiveThreadNum);
        } else {
            _receiver = new CoroutineReceiver(this);
        }
        
        // 根据需要启动worker协程或线程
        if (_workThreadNum > 0) {
            _worker = new MultiThreadWorker(this, _workThreadNum);
        } else {
            _worker = new CoroutineWorker(this);
        }
        
        if (!_acceptor->start()) {
            printf("ERROR: Server::start() -- start acceptor failed.\n");
            return false;
        }
        
        if (!_receiver->start()) {
            printf("ERROR: Server::start() -- start receiver failed.\n");
            return false;
        }
        
        if (!_worker->start()) {
            printf("ERROR: Server::start() -- start worker failed.\n");
            return false;
        }
        
        return true;
    }

    void *Server::Acceptor::acceptRoutine( void * arg ) {
        Acceptor *self = (Acceptor *)arg;
        co_enable_hook_sys();
        
        Receiver *receiver = self->_server->getReceiver();
        
        // 初始化receiver的pipe writefd
        receiver->initForAcceptor();
        
        int listen_fd = self->_listen_fd;
        // 侦听连接，并把接受的连接传给连接处理对象
        for(;;)
        {
            struct sockaddr_in addr; //maybe sockaddr_un;
            memset( &addr,0,sizeof(addr) );
            socklen_t len = sizeof(addr);
            
            printf("INFO: try accept from listen fd %d\n", listen_fd);
            int fd = co_accept(listen_fd, (struct sockaddr *)&addr, &len);
            if( fd < 0 )
            {
                struct pollfd pf = { 0 };
                pf.fd = listen_fd;
                pf.events = (POLLIN|POLLERR|POLLHUP);
                co_poll( co_get_epoll_ct(),&pf,1,1000 );
                continue;
            }
            
            printf("INFO: accept fd %d\n", fd);
            
            co_set_timeout(fd, -1, 1000);
            // 将接受的连接fd交给Receiver处理
            receiver->postConnection(fd);
        }
        
        return NULL;
    }
    
    Server::Acceptor::~Acceptor() {
        
    }
    
    bool Server::Acceptor::init() {
        const std::string &ip = _server->getIP();
        uint16_t port = _server->getPort();
        
        _listen_fd = socket(AF_INET,SOCK_STREAM, IPPROTO_TCP);
        if( _listen_fd >= 0 )
        {
            if(port != 0)
            {
                int nReuseAddr = 1;
                setsockopt(_listen_fd,SOL_SOCKET,SO_REUSEADDR,&nReuseAddr,sizeof(nReuseAddr));
                
                struct sockaddr_in addr ;
                bzero(&addr,sizeof(addr));
                addr.sin_family = AF_INET;
                addr.sin_port = htons(port);
                int nIP = 0;
                
                if (ip.empty() ||
                    ip.compare("0") == 0 ||
                    ip.compare("0.0.0.0") == 0 ||
                    ip.compare("*") == 0) {
                    nIP = htonl(INADDR_ANY);
                }
                else
                {
                    nIP = inet_addr(ip.c_str());
                }
                addr.sin_addr.s_addr = nIP;
                
                int ret = bind(_listen_fd,(struct sockaddr*)&addr,sizeof(addr));
                if( ret != 0)
                {
                    close(_listen_fd);
                    _listen_fd = -1;
                }
            }
        }
        
        if(_listen_fd==-1){
            printf("ERROR: Acceptor::start() -- Port %d is in use\n", port);
            return false;
        }
        
        printf("INFO: Acceptor::start() -- listen %d %s:%d\n", _listen_fd, ip.c_str(), port);
        listen( _listen_fd, 1024 );
        
        int iFlags;
        iFlags = fcntl(_listen_fd, F_GETFL, 0);
        iFlags |= O_NONBLOCK;
        iFlags |= O_NDELAY;
        fcntl(_listen_fd, F_SETFL, iFlags);
        
        return true;
    }
    
    void Server::ThreadAcceptor::threadEntry(ThreadAcceptor *self) {
        if (!self->init()) {
            printf("ERROR: ThreadAcceptor::threadEntry() -- init fail\n");
            return;
        }
        
        // 启动accept协程
        RoutineEnvironment::startCoroutine(acceptRoutine, self);
        
        RoutineEnvironment::runEventLoop();
    }
    
    bool Server::ThreadAcceptor::start() {
        
        _t = std::thread(threadEntry, this);
        
        return true;
    }
    
    bool Server::CoroutineAcceptor::start() {
        if (!init()) {
            return false;
        }
        
        RoutineEnvironment::startCoroutine(acceptRoutine, this);
        
        return true;
    }
    
    Server::Receiver::~Receiver() {
        
    }
    
    void *Server::Receiver::connectDispatchRoutine( void * arg ) {
        QueueContext *context = (QueueContext*)arg;
        
        ServerConnectFdQueue& queue = context->_queue;
        Receiver *receiver = context->_receiver;
        
        // 初始化pipe readfd
        co_enable_hook_sys();
        int readFd = queue.getReadFd();
        co_register_fd(readFd);
        co_set_timeout(readFd, -1, 1000);
        
        int ret;
        int hopeNum = 1;
        while (true) {
            std::vector<char> buf(hopeNum);
            int total_read_num = 0;
            while (total_read_num < hopeNum) {
                ret = read(readFd, &buf[0] + total_read_num, hopeNum - total_read_num);
                assert(ret != 0);
                if (ret < 0) {
                    if (errno == EAGAIN) {
                        continue;
                    }
                    
                    // 管道出错
                    printf("ERROR: MultiThreadReceiver::connectDispatchRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                           readFd, ret, errno, strerror(errno));
                    
                    // FIXME: 如何处理？退出协程？
                    // sleep 100 milisecond
                    struct pollfd pf = { 0 };
                    pf.fd = -1;
                    poll( &pf,1,100);
                    
                    break;
                }
                
                total_read_num += ret;
            }
            
            hopeNum = 0;
            // 处理任务队列
            int fd = queue.pop();
            while (fd) {
                hopeNum++;
                
                // 创建协程处理接受的连接的数据收发，若连接断线，结束协程并回收协程资源
                // 问题：由于rpc处理会交给worker进行，worker完成rpc处理后会将结果返回给连接协程进行发送，但此时连接协程可能已经销毁，如何解决？
                // 答：处理一：如果数据收发协程的处理流程是：1.接收rpc请求数据 2.将收到的请求发给worker 3.等待worker处理完成 4.回传rpc结果 5.回到1继续。
                //    此方法当一个rpc没有完成不会接受新的rpc请求（效率低），其中的1和4可能因断线导致协程终止，此时不会出现上面描述的问题。
                //    但若在第3步进行超时处理，就有可能导致上面描述的问题。因此不应在第3步进行超时处理。
                //    处理二：如果数据收发协程的处理流程是：1.接收rpc请求数据（有超时4ms）2.若收到请求发给worker处理 3.若有rpc结果完成回传rpc结果 4.回到1继续。
                //    此方法会同时接受多个rpc请求进行处理（效率高），可能出现上面描述的问题。应该可以对结果队列进行smart_ptr，仅当没有引用时才真正释放。
                // 用处理一的方式（如果要实现更高的并发性能，可以建立多个连接），这样的话，worker需要改为在主逻辑线程执行或接收器中直接执行两种方式
                // 用处理二的方式，对协程上下文对象用smart指针引用
                // 选哪种？ 本实现选择处理二
                
                ConnectedContext *arg = new ConnectedContext;
                arg->receiver = receiver;
                arg->fd = fd;
                
                RoutineEnvironment::startCoroutine(connectHandleRoutine, arg);
                
                fd = queue.pop();
            }
            
            if (hopeNum == 0) {
                printf("WARN: MultiThreadReceiver::connectDispatchRoutine no task in queue\n");
                
                hopeNum = 1;
            }
        }
    }
    
    void *Server::Receiver::connectHandleRoutine( void * arg ) {
        co_enable_hook_sys();
        ConnectedContext *connectedContext = (ConnectedContext *)arg;
        Receiver *self = connectedContext->receiver;
        int fd = connectedContext->fd;
        delete connectedContext;
        
        printf("INFO: start connectHandleRoutine for fd:%d in thread:%d\n", fd, GetPid());
        
        // 本协程的结果处理队列，需要传给worker，因此使用智能指针
        std::shared_ptr<ServerRpcResultQueue> rqueue(new ServerRpcResultQueue); // result queue
        
        // 接收rpc请求数据
        int times = 0;
        int timeout = 8; // 初始超时设置为8ms
        int ret = 0;
        bool closed = false;
        bool somethingHappen = true;
        
        RpcRequestHead reqhead;
        char *head_buf = (char *)(&reqhead);
        int head_len = sizeof(reqhead);
        while (!closed) {
            if (!somethingHappen) {
                if (timeout != 0x400) {
                    times++;
                    if (times == 100) {
                        timeout = timeout << 1;
                        times = 0;
                    }
                }
            } else {
                timeout = 8; // 恢复初始超时时间8ms
                times = 0;
                somethingHappen = false;
            }
            
            // 等待10ms看看是否有请求到达
            // 注意：经过分析，当事件循环中没有fd事件发生时，事件循环会等待1秒超时后才会处理超时事件，达不到10ms的粒度
            // 目前已经把事件循环超时时间改为100ms
            // 其实可以改造成弹性超时等待时间，初始超时时间设置为4ms,当超时发生时（无事件发生），下次超时时间加倍，最大超时时间为1秒，当没有超时时（有事件发生），将超时时间恢复回初始超时时间
            // 这样能避免系统空转
            struct pollfd pf = { 0 };
            pf.fd = fd;
            pf.events = (POLLIN|POLLERR|POLLHUP);
            ret = co_poll( co_get_epoll_ct(),&pf,1,timeout);
            
            if (ret > 0) {
                somethingHappen = true;
                
                // FIXME: 这里应该改为一次尽可能多的读取数据到预先分配的缓存中，然后再从缓存中进行解析，减少fd读取次数和协程切换次数（性能开销特别大）
                
                // 读取rpc请求
                int total_read_num = 0;
                while (total_read_num < head_len) {
                    ret = read(fd, head_buf + total_read_num, head_len - total_read_num);
                    
                    if (ret <= 0) {
                        // ret 0 mean disconnected
                        if (ret < 0 && errno == EAGAIN) {
                            continue;
                        }
                        
                        // 出错处理
                        printf("ERROR: Receiver::connectHandleRoutine -- read reqhead fd %d ret %d errno %d (%s)\n",
                               fd, ret, errno, strerror(errno));
                        
                        close(fd);
                        closed = true;
                        break;
                    }
                    
                    total_read_num += ret;
                    
                }
                
                if (closed) {
                    break;
                }
                
                assert(reqhead.size > 0);
                
                // read rpc body
                std::vector<char> buf(reqhead.size);
                total_read_num = 0;
                while (total_read_num < reqhead.size) {
                    ret = read(fd, &buf[0] + total_read_num, reqhead.size - total_read_num);
                    if (ret <= 0) {
                        // ret 0 mean disconnected
                        if (ret < 0 && errno == EAGAIN) {
                            continue;
                        }
                        
                        // 出错处理
                        printf("ERROR: Receiver::connectHandleRoutine -- read body fd %d ret %d errno %d (%s)\n",
                               fd, ret, errno, strerror(errno));
                        
                        close(fd);
                        closed = true;
                        break;
                    }
                    
                    total_read_num += ret;
                }
                
                if (closed) {
                    break;
                }
                
                // 生成ServerRpcTask
                // 根据serverId和methodId查表
                const MethodData *methodData = self->_server->getMethod(reqhead.serviceId, reqhead.methodId);
                if (methodData != NULL) {
                    google::protobuf::Message *request = methodData->_request_proto->New();
                    google::protobuf::Message *response = methodData->_response_proto->New();
                    Controller *controller = new Controller();
                    if (!request->ParseFromArray(&buf[0], buf.size())) {
                        // 出错处理
                        printf("ERROR: Receiver::connectHandleRoutine -- parse request body fail\n");
                        
                        close(fd);
                        closed = true;
                        break;
                    }
                    
                    // 将收到的请求传给worker
                    ServerWorkerTask *task = new ServerWorkerTask;
                    task->queue = rqueue;
                    task->rpcTask = new ServerRpcTask;
                    task->rpcTask->service = self->_server->getService(reqhead.serviceId);
                    task->rpcTask->method_descriptor = methodData->_method_descriptor;
                    task->rpcTask->request = request;
                    task->rpcTask->response = response;
                    task->rpcTask->controller = controller;
                    task->rpcTask->callId = reqhead.callId;
                    
                    // 传给worker
                    self->_server->getWorker()->postRpcTask(task);
                } else {
                    // 出错处理
                    printf("ERROR: Receiver::connectHandleRoutine -- can't find method object of serviceId: %u methodId: %u\n", reqhead.serviceId, reqhead.methodId);
                    
                    close(fd);
                    closed = true;
                    break;
                }
            }
            
            // 检测结果队列是否有需要返回的结果，若有则返回
            ServerRpcTask *task = rqueue->pop();
            if (task) {
                somethingHappen = true;
            }
            
            while (task) {
                // TODO: 根据controller的结果返回出错信息（在包头加入出错信息）
                
                // 返回结果
                // 准备返回的头部和数据
                RpcResponseHead resphead;
                resphead.callId = task->callId;
                
                std::string data;
                task->response->SerializeToString(&data);
                resphead.size = data.size();
                
                delete task->request;
                delete task->response;
                delete task->controller;
                delete task;
                
                // 发送头部
                ret = write(fd, &resphead, sizeof(resphead));
                if (ret <= 0) {
                    printf("ERROR: Receiver::connectHandleRoutine -- write resphead fd %d ret %d errno %d (%s)\n",
                           fd, ret, errno, strerror(errno));
                    
                    close(fd);
                    closed = true;
                    break;
                }
                assert(ret == sizeof(resphead));
                
                // 发送包体
                ret = write(fd, data.c_str(), data.size());
                if (ret <= 0) {
                    printf("ERROR: Receiver::connectHandleRoutine -- write body fd %d ret %d errno %d (%s)\n",
                           fd, ret, errno, strerror(errno));
                    
                    close(fd);
                    closed = true;
                    break;
                }
                assert(ret == data.size());
                
                task = rqueue->pop();
            }
        }
        
        printf("INFO: end connectHandleRoutine for fd:%d in thread:%d\n", fd, GetPid());
        
        return NULL;
    }
    
    void Server::MultiThreadReceiver::threadEntry(ThreadData *tdata) {
        // 启动处理待处理连接协程
        RoutineEnvironment::startCoroutine(connectDispatchRoutine, &tdata->_queueContext);
        
        RoutineEnvironment::runEventLoop(1000);
    }
    
    void Server::MultiThreadReceiver::initForAcceptor() {
        for (std::vector<ThreadData>::iterator it = _threadDatas.begin(); it != _threadDatas.end(); it++) {
            co_register_fd(it->_queueContext._queue.getWriteFd());
        }
    }
    
    bool Server::MultiThreadReceiver::start() {
        // 启动线程
        for (std::vector<ThreadData>::iterator it = _threadDatas.begin(); it != _threadDatas.end(); it++) {
            it->_queueContext._receiver = this;
            it->_t = std::thread(threadEntry, &(*it));
        }
        
        return true;
    }
    
    void Server::MultiThreadReceiver::postConnection(int fd) {
        _lastThreadIndex = (_lastThreadIndex + 1) % _threadNum;
        
        _threadDatas[_lastThreadIndex]._queueContext._queue.push(fd);
    }
    
    void Server::CoroutineReceiver::initForAcceptor() {
        co_register_fd(_queueContext._queue.getWriteFd());
    }
    
    bool Server::CoroutineReceiver::start() {
        RoutineEnvironment::startCoroutine(connectDispatchRoutine, &_queueContext);
        
        return true;
    }
    
    void Server::CoroutineReceiver::postConnection(int fd) {
        _queueContext._queue.push(fd);
    }
    
    Server::Worker::~Worker() {
        
    }

    // pipe通知版本
    void *Server::Worker::taskHandleRoutine( void * arg ) {
        QueueContext *context = (QueueContext*)arg;
        
        ServerWorkerTaskQueue& tqueue = context->_queue;
        Worker *self = context->_worker;
        
        // 初始化pipe readfd
        co_enable_hook_sys();
        int readFd = tqueue.getReadFd();
        co_register_fd(readFd);
        co_set_timeout(readFd, -1, 1000);
        
        int ret;
        int hopeNum = 1;
        while (true) {
            std::vector<char> buf(hopeNum);
            int total_read_num = 0;
            while (total_read_num < hopeNum) {
                ret = read(readFd, &buf[0] + total_read_num, hopeNum - total_read_num);
                
                assert(ret != 0);
                if (ret < 0) {
                    if (errno == EAGAIN) {
                        continue;
                    }
                    
                    // 管道出错
                    printf("ERROR: MultiThreadReceiver::connectDispatchRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                           readFd, ret, errno, strerror(errno));
                    
                    // FIXME: 如何处理？退出协程？
                    // sleep 100 milisecond
                    struct pollfd pf = { 0 };
                    pf.fd = -1;
                    poll( &pf,1,100);
                    
                    break;
                }
                
                total_read_num += ret;
            }
            
            hopeNum = 0;
            // 处理任务队列
            ServerWorkerTask *task = tqueue.pop();
            while (task) {
                hopeNum++;
                
                bool needCoroutine = task->rpcTask->method_descriptor->options().GetExtension(corpc::need_coroutine);
                
                if (needCoroutine) {
                    // 启动协程进行rpc处理
                    RoutineEnvironment::startCoroutine(taskCallRoutine, task);
                } else {
                    // rpc处理方法调用
                    task->rpcTask->service->CallMethod(task->rpcTask->method_descriptor, task->rpcTask->controller, task->rpcTask->request, task->rpcTask->response, NULL);
                    
                    // 处理结果发回给receiver处理
                    task->queue->push(task->rpcTask);
                    
                    delete task;
                }
                
                task = tqueue.pop();
            }
            
            if (hopeNum == 0) {
                printf("WARN: Worker::taskHandleRoutine no task in queue\n");
            
                hopeNum = 1;
            }
        }
        
        return NULL;
    }

    /*
    // 轮询版本
    void *Server::Worker::taskHandleRoutine( void * arg ) {
        ServerWorkerTaskQueue *tqueue = (ServerWorkerTaskQueue*)arg;
        
        // 初始化pipe readfd
        co_enable_hook_sys();

        while (true) {
            struct pollfd pf = { 0 };
            pf.fd = -1;
            poll( &pf,1,4);
            
            ServerWorkerTask *task = tqueue->pop();
            while (task) {
                // rpc处理方法调用
                task->rpcTask->service->CallMethod(task->rpcTask->method_descriptor, task->rpcTask->controller, task->rpcTask->request, task->rpcTask->response, NULL);
                
                // 处理结果发回给receiver处理
                task->queue->push(task->rpcTask);
                
                delete task;
                
                task = tqueue->pop();
            }
        }
        
        return NULL;
    }
    */
    
    void *Server::Worker::taskCallRoutine( void * arg ) {
        ServerWorkerTask *task = (ServerWorkerTask *)arg;
        
        task->rpcTask->service->CallMethod(task->rpcTask->method_descriptor, task->rpcTask->controller, task->rpcTask->request, task->rpcTask->response, NULL);
        
        // 处理结果发回给receiver处理
        task->queue->push(task->rpcTask);
        
        delete task;
        
        return NULL;
    }
    
    void Server::MultiThreadWorker::threadEntry( ThreadData *tdata ) {
        // 启动rpc任务处理协程
        RoutineEnvironment::startCoroutine(taskHandleRoutine, &tdata->_queueContext);
        
        RoutineEnvironment::runEventLoop();
    }
    
    bool Server::MultiThreadWorker::start() {
        // 启动线程
        for (std::vector<ThreadData>::iterator it = _threadDatas.begin(); it != _threadDatas.end(); it++) {
            it->_queueContext._worker = this;
            it->_t = std::thread(threadEntry, &(*it));
        }
        
        return true;
    }
    
    void Server::MultiThreadWorker::postRpcTask(ServerWorkerTask *task) {
        uint16_t index = (_lastThreadIndex + 1) % _threadNum;
        // FIXME: 对_lastThreadIndex的处理是否需要加锁？
        _lastThreadIndex = index;
        _threadDatas[index]._queueContext._queue.push(task);
    }
    
    bool Server::CoroutineWorker::start() {
        // 启动rpc任务处理协程
        RoutineEnvironment::startCoroutine(taskHandleRoutine, &_queueContext);
        
        return true;
    }
    
    void Server::CoroutineWorker::postRpcTask(ServerWorkerTask *task) {
        _queueContext._queue.push(task);
    }
    
}
