//
//  co_rpc_server.cpp
//  rpcsvr
//
//  Created by Xianke Liu on 2017/11/21.
//  Copyright © 2017年 Dena. All rights reserved.
//
#include "corpc_routine_env.h"

#include "corpc_server.h"

#include "corpc_controller.h"

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
    Server::Connection::Connection(int fd, Server* server): IO::Connection(fd, server->_io), _server(server), _reqdata(CORPC_MAX_REQUEST_SIZE,0), _headNum(0), _dataNum(0) {
        _head_buf = (char *)(&_reqhead);
        _data_buf = (uint8_t *)_reqdata.data();
    }
    
    Server::Connection::~Connection() {
        printf("INFO: Server::Connection::~Connection -- fd:%d in thread:%d\n", _fd, GetPid());
    }
    
    bool Server::Connection::parseData(uint8_t *buf, int size) {
        // 解析数据
        int offset = 0;
        while (size > offset) {
            // 先解析头部
            if (_headNum < sizeof(RpcRequestHead)) {
                int needNum = sizeof(RpcRequestHead) - _headNum;
                if (size - offset > needNum) {
                    memcpy(_head_buf + _headNum, buf + offset, needNum);
                    _headNum = sizeof(RpcRequestHead);
                    
                    offset += needNum;
                } else {
                    memcpy(_head_buf + _headNum, buf + offset, size - offset);
                    _headNum += size - offset;
                    
                    break;
                }
            }
            
            if (_reqhead.size > CORPC_MAX_REQUEST_SIZE) { // 数据超长
                printf("ERROR: Server::Connection::parseData -- request too large for fd:%d in thread:%d\n", _fd, GetPid());
                
                return false;
            }
            
            // 从缓存中解析数据
            if (_dataNum < _reqhead.size) {
                int needNum = _reqhead.size - _dataNum;
                if (size - offset >= needNum) {
                    memcpy(_data_buf + _dataNum, buf + offset, needNum);
                    _dataNum = _reqhead.size;
                    
                    offset += needNum;
                } else {
                    memcpy(_data_buf + _dataNum, buf + offset, size - offset);
                    _dataNum += size - offset;
                    
                    break;
                }
            }
            
            // 生成ServerRpcTask
            // 根据serverId和methodId查表
            const MethodData *methodData = _server->getMethod(_reqhead.serviceId, _reqhead.methodId);
            if (methodData != NULL) {
                google::protobuf::Message *request = methodData->_request_proto->New();
                google::protobuf::Message *response = methodData->_response_proto->New();
                Controller *controller = new Controller();
                if (!request->ParseFromArray(_data_buf, _reqhead.size)) {
                    // 出错处理
                    printf("ERROR: Server::Connection::parseData -- parse request body fail\n");
                    
                    return false;
                }
                
                // 将收到的请求传给worker
                WorkerTask *task = new WorkerTask;
                task->connection = shared_from_this();
                task->rpcTask = std::shared_ptr<RpcTask>(new RpcTask);
                task->rpcTask->service = _server->getService(_reqhead.serviceId);
                task->rpcTask->method_descriptor = methodData->_method_descriptor;
                task->rpcTask->request = request;
                task->rpcTask->response = response;
                task->rpcTask->controller = controller;
                task->rpcTask->callId = _reqhead.callId;
                
                // 传给worker
                _server->_worker->postRpcTask(task);
            } else {
                // 出错处理
                printf("ERROR: Server::Connection::parseData -- can't find method object of serviceId: %u methodId: %u\n", _reqhead.serviceId, _reqhead.methodId);
                
                return false;
            }
            
            // 处理完一个请求消息，复位状态
            _headNum = 0;
            _dataNum = 0;
        }
        
        return true;
    }
    
    int Server::Connection::buildData(uint8_t *buf, int space) {
        int used = 0;
        while (_datas.size() > 0) {
            std::shared_ptr<RpcTask> rpcTask = std::static_pointer_cast<RpcTask>(_datas.front());
            int msgSize = rpcTask->response->GetCachedSize();
            if (msgSize == 0) {
                msgSize = rpcTask->response->ByteSize();
            }
            
            if (msgSize + sizeof(RpcResponseHead) >= space - used) {
                break;
            }
            
            // head
            RpcResponseHead resphead;
            resphead.callId = rpcTask->callId;
            resphead.size = msgSize;
            
            memcpy(buf + used, &resphead, sizeof(RpcResponseHead));
            used += sizeof(RpcResponseHead);
            
            rpcTask->response->SerializeWithCachedSizesToArray(buf + used);
            used += msgSize;
            
            _datas.pop_front();
        }
        
        return used;
    }
    
    Server::RpcTask::RpcTask(): service(NULL), method_descriptor(NULL), request(NULL), response(NULL), controller(NULL) {
        
    }
    
    Server::RpcTask::~RpcTask() {
        delete request;
        delete response;
        delete controller;
    }
    
    void *Server::Acceptor::acceptRoutine( void * arg ) {
        Acceptor *self = (Acceptor *)arg;
        co_enable_hook_sys();
        
        IO *io = self->_server->_io;
        
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
            
            // 设置读写超时时间，默认为1秒
            co_set_timeout(fd, -1, 1000);
            
            std::shared_ptr<IO::Connection> connection(new Connection(fd, self->_server));
            
            // 将接受的连接分别发给Receiver和Sender
            io->addConnection(connection);
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
    
    Server::Worker::~Worker() {
        
    }

    // pipe通知版本
    void *Server::Worker::taskHandleRoutine( void * arg ) {
        QueueContext *context = (QueueContext*)arg;
        
        WorkerTaskQueue& tqueue = context->_queue;
        Worker *self = context->_worker;
        IO *io = self->_server->_io;
        
        // 初始化pipe readfd
        co_enable_hook_sys();
        int readFd = tqueue.getReadFd();
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
                    printf("ERROR: MultiThreadReceiver::connectDispatchRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                           readFd, ret, errno, strerror(errno));
                    
                    // TODO: 如何处理？退出协程？
                    // sleep 10 milisecond
                    struct pollfd pf = { 0 };
                    pf.fd = -1;
                    poll( &pf,1,10);
                }
            }
            
            // 处理任务队列
            WorkerTask *task = tqueue.pop();
            while (task) {
                bool needCoroutine = task->rpcTask->method_descriptor->options().GetExtension(corpc::need_coroutine);
                
                if (needCoroutine) {
                    // 启动协程进行rpc处理
                    RoutineEnvironment::startCoroutine(taskCallRoutine, task);
                } else {
                    // rpc处理方法调用
                    task->rpcTask->service->CallMethod(task->rpcTask->method_descriptor, task->rpcTask->controller, task->rpcTask->request, task->rpcTask->response, NULL);
                    
                    // 处理结果发给sender处理
                    io->getSender()->send(task->connection, task->rpcTask);
                    
                    delete task;
                }
                
                task = tqueue.pop();
            }
        }
        
        return NULL;
    }

    /*
    // 轮询版本
    void *Server::Worker::taskHandleRoutine( void * arg ) {
        QueueContext *context = (QueueContext*)arg;
     
        WorkerTaskQueue& tqueue = context->_queue;
        Worker *self = context->_worker;
        IO *io = self->_server->getIO();
     
        // 初始化pipe readfd
        co_enable_hook_sys();

        while (true) {
            struct pollfd pf = { 0 };
            pf.fd = -1;
            poll( &pf,1,4);
            
            WorkerTask *task = tqueue->pop();
            while (task) {
     
                bool needCoroutine = task->rpcTask->method_descriptor->options().GetExtension(corpc::need_coroutine);
     
                if (needCoroutine) {
                    // 启动协程进行rpc处理
                    RoutineEnvironment::startCoroutine(taskCallRoutine, task);
                } else {
                    // rpc处理方法调用
                    task->rpcTask->service->CallMethod(task->rpcTask->method_descriptor, task->rpcTask->controller, task->rpcTask->request, task->rpcTask->response, NULL);
     
                    // 处理结果发回给receiver处理
                    io->getSender()->send(task->connection, task->rpcTask);
     
                    delete task;
                }
     
                task = tqueue.pop();
            }
        }
        
        return NULL;
    }
    */
    
    void *Server::Worker::taskCallRoutine( void * arg ) {
        WorkerTask *task = (WorkerTask *)arg;
        
        task->rpcTask->service->CallMethod(task->rpcTask->method_descriptor, task->rpcTask->controller, task->rpcTask->request, task->rpcTask->response, NULL);
        
        // 处理结果发回给receiver处理
        task->connection->send(task->rpcTask);
        
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
    
    void Server::MultiThreadWorker::postRpcTask(WorkerTask *task) {
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
    
    void Server::CoroutineWorker::postRpcTask(WorkerTask *task) {
        _queueContext._queue.push(task);
    }
    
    Server::Server(IO *io, bool acceptInNewThread, uint16_t workThreadNum, const std::string& ip, uint16_t port): _io(io), _acceptInNewThread(acceptInNewThread), _workThreadNum(workThreadNum), _ip(ip), _port(port) {
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
    
    const Server::MethodData *Server::getMethod(uint32_t serviceId, uint32_t methodId) const {
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
        
        if (!_worker->start()) {
            printf("ERROR: Server::start() -- start worker failed.\n");
            return false;
        }
        
        return true;
    }

}
