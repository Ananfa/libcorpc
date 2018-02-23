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
    Server::Connection::Connection(int fd, Receiver* receiver, Sender* sender): _fd(fd), _receiver(receiver), _sender(sender), _routineHang(false), _routine(NULL), _isClosing(false), _canClose(false) {
    }
    
    Server::Connection::~Connection() {
        for (std::list<RpcTask*>::iterator iter = _respList.begin(); iter != _respList.end(); iter++) {
            RpcTask *task = *iter;
            delete task->request;
            delete task->response;
            delete task->controller;
            delete task;
        }
        
        _respList.clear();
    }
    
    void *Server::Acceptor::acceptRoutine( void * arg ) {
        Acceptor *self = (Acceptor *)arg;
        co_enable_hook_sys();
        
        Receiver *receiver = self->_server->getReceiver();
        Sender *sender = self->_server->getSender();
        
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
            
            std::shared_ptr<Connection> connection(new Connection(fd, receiver, sender));
            
            // 将接受的连接分别发给Receiver和Sender
            receiver->addConnection(connection);
            sender->addConnection(connection);
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
    
    void *Server::Receiver::connectionDispatchRoutine( void * arg ) {
        QueueContext *context = (QueueContext*)arg;
        
        ReceiverTaskQueue& queue = context->_queue;
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
            ReceiverTask* recvTask = queue.pop();
            while (recvTask) {
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
                
                RoutineEnvironment::startCoroutine(connectionRoutine, recvTask);
                
                recvTask = queue.pop();
            }
            
            if (hopeNum == 0) {
                printf("WARN: MultiThreadReceiver::connectDispatchRoutine no task in queue\n");
                
                hopeNum = 1;
            }
        }
    }
    
    void *Server::Receiver::connectionRoutine( void * arg ) {
        co_enable_hook_sys();
        ReceiverTask *recvTask = (ReceiverTask *)arg;
        //Receiver *self = connectedContext->receiver;
        std::shared_ptr<Connection> connection = recvTask->connection;
        delete recvTask;
        
        Server *server = connection->getReceiver()->_server;
        Worker *worker = server->getWorker();
        
        int fd = connection->getfd();
        printf("INFO: start connectHandleRoutine for fd:%d in thread:%d\n", fd, GetPid());
        
        // 接收rpc请求数据
        int ret = 0;
        
        RpcRequestHead reqhead;
        char *head_buf = (char *)(&reqhead);
        int head_len = sizeof(reqhead);
        while (true) {
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
                    
                    goto CLOSE_CONNECTION;
                }
                
                total_read_num += ret;
                
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
                    
                    goto CLOSE_CONNECTION;
                }
                
                total_read_num += ret;
            }
            
            // 生成ServerRpcTask
            // 根据serverId和methodId查表
            const MethodData *methodData = server->getMethod(reqhead.serviceId, reqhead.methodId);
            if (methodData != NULL) {
                google::protobuf::Message *request = methodData->_request_proto->New();
                google::protobuf::Message *response = methodData->_response_proto->New();
                Controller *controller = new Controller();
                if (!request->ParseFromArray(&buf[0], buf.size())) {
                    // 出错处理
                    printf("ERROR: Receiver::connectHandleRoutine -- parse request body fail\n");
                    
                    goto CLOSE_CONNECTION;
                }
                
                // 将收到的请求传给worker
                WorkerTask *task = new WorkerTask;
                task->connection = connection;
                task->rpcTask = new RpcTask;
                task->rpcTask->service = server->getService(reqhead.serviceId);
                task->rpcTask->method_descriptor = methodData->_method_descriptor;
                task->rpcTask->request = request;
                task->rpcTask->response = response;
                task->rpcTask->controller = controller;
                task->rpcTask->callId = reqhead.callId;
                
                // 传给worker
                worker->postRpcTask(task);
            } else {
                // 出错处理
                printf("ERROR: Receiver::connectHandleRoutine -- can't find method object of serviceId: %u methodId: %u\n", reqhead.serviceId, reqhead.methodId);
                
                goto CLOSE_CONNECTION;
            }
        }
        
CLOSE_CONNECTION:
        connection->_sender->removeConnection(connection); // 通知sender关闭connection
        shutdown(fd, SHUT_WR);  // 让sender中的fd相关协程退出
        
        // 等待写关闭
        while (!connection->_canClose) {
            // sleep 100 milisecond
            struct pollfd pf = { 0 };
            pf.fd = -1;
            poll( &pf,1,100);
        }
        
        close(fd);
        printf("INFO: end connectHandleRoutine for fd:%d in thread:%d\n", fd, GetPid());
        
        return NULL;
    }
    
    void Server::MultiThreadReceiver::threadEntry(ThreadData *tdata) {
        // 启动处理待处理连接协程
        RoutineEnvironment::startCoroutine(connectionDispatchRoutine, &tdata->_queueContext);
        
        RoutineEnvironment::runEventLoop();
    }
    
    bool Server::MultiThreadReceiver::start() {
        // 启动线程
        for (std::vector<ThreadData>::iterator it = _threadDatas.begin(); it != _threadDatas.end(); it++) {
            it->_queueContext._receiver = this;
            it->_t = std::thread(threadEntry, &(*it));
        }
        
        return true;
    }
    
    void Server::MultiThreadReceiver::addConnection(std::shared_ptr<Connection> connection) {
        _lastThreadIndex = (_lastThreadIndex + 1) % _threadNum;
        
        connection->setRecvThreadIndex(_lastThreadIndex);
        
        ReceiverTask *recvTask = new ReceiverTask;
        recvTask->connection = connection;
        
        _threadDatas[_lastThreadIndex]._queueContext._queue.push(recvTask);
    }
    
    bool Server::CoroutineReceiver::start() {
        RoutineEnvironment::startCoroutine(connectionDispatchRoutine, &_queueContext);
        
        return true;
    }
    
    void Server::CoroutineReceiver::addConnection(std::shared_ptr<Connection> connection) {
        connection->setRecvThreadIndex(0);
        
        ReceiverTask *recvTask = new ReceiverTask;
        recvTask->connection = connection;
        
        _queueContext._queue.push(recvTask);
    }
    
    Server::Sender::~Sender() {
        
    }
    
    void *Server::Sender::taskQueueRoutine( void * arg ) {
        QueueContext *context = (QueueContext*)arg;
        
        SenderTaskQueue& queue = context->_queue;
        Sender *sender = context->_sender;
        
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
                    printf("ERROR: Server::Sender::taskQueueRoutine read from pipe fd %d ret %d errno %d (%s)\n",
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
            SenderTask *task = queue.pop();
            while (task) {
                hopeNum++;
                
                switch (task->type) {
                    case SenderTask::INIT:
                        RoutineEnvironment::startCoroutine(connectionRoutine, task);
                        break;
                        
                    case SenderTask::CLOSE:
                        if (!task->connection->_isClosing) {
                            task->connection->_isClosing = true;
                            
                            if (task->connection->_routineHang) {
                                co_resume(task->connection->_routine);
                            }
                        }
                        
                        delete task;
                        break;
                        
                    case SenderTask::DATA:
                        // 若连接未关闭，放入connection的等待发送队列，若connection协程挂起则唤醒执行
                        if (task->connection->_isClosing) {
                            delete task->rpcTask->request;
                            delete task->rpcTask->response;
                            delete task->rpcTask->controller;
                            delete task->rpcTask;
                        } else {
                            task->connection->_respList.push_back(task->rpcTask);
                            
                            // 唤醒挂起的connection协程
                            if (task->connection->_routineHang) {
                                co_resume(task->connection->_routine);
                            }
                        }
                        
                        delete task;
                        break;
                }
                
                task = queue.pop();
            }
            
            if (hopeNum == 0) {
                printf("WARN: Server::Sender::taskQueueRoutine no task in queue\n");
                
                hopeNum = 1;
            }
        }
        
        return NULL;
    }
    
    void *Server::Sender::connectionRoutine( void * arg ) {
        // 注意: 参数不能传shared_ptr所管理的指针，另外要考虑shared_ptr的多线程问题
        SenderTask *task = (SenderTask*)arg;
        assert(task->type == SenderTask::INIT);
        std::shared_ptr<Connection> connection = task->connection;
        delete task;
        
        connection->_routine = co_self();
        connection->_routineHang = false;
        
        std::string buffs(CORPC_MAX_BUFFER_SIZE,0);
        uint8_t *buf = (uint8_t *)buffs.data();
        uint32_t startIndex = 0;
        uint32_t endIndex = 0;
        
        // 若无数据可以发送则挂起，否则整理发送数据并发送
        while (true) {
            if (connection->_isClosing) {
                break;
            }
            
            while (connection->_respList.size() > 0) {
                RpcTask *rpcTask = connection->_respList.front();
                int msgSize = rpcTask->response->GetCachedSize();
                if (msgSize == 0) {
                    msgSize = rpcTask->response->ByteSize();
                }
                
                if (msgSize + sizeof(RpcResponseHead) >= CORPC_MAX_BUFFER_SIZE - endIndex) {
                    break;
                }
                
                // head
                RpcResponseHead resphead;
                resphead.callId = rpcTask->callId;
                resphead.size = msgSize;
                
                memcpy(buf + endIndex, &resphead, sizeof(RpcResponseHead));
                endIndex += sizeof(RpcResponseHead);
                
                rpcTask->response->SerializeWithCachedSizesToArray(buf + endIndex);
                endIndex += msgSize;
                
                connection->_respList.pop_front();
                
                delete rpcTask->request;
                delete rpcTask->response;
                delete rpcTask->controller;
                delete rpcTask;
            }
            
            int dataSize = endIndex - startIndex;
            if (dataSize == 0) {
                assert(startIndex == 0);
                
                // 挂起
                connection->_routineHang = true;
                co_yield_ct();
                connection->_routineHang = false;
                
                continue;
            }
            
            // 发数据
            int ret = write(connection->_fd, buf + startIndex, dataSize);
            if (ret < 0) {
                printf("ERROR: Server::Sender::connectionRoutine -- write resphead fd %d ret %d errno %d (%s)\n",
                       connection->_fd, ret, errno, strerror(errno));
                
                break;
            }
            
            startIndex += ret;
            if (startIndex == endIndex) {
                startIndex = endIndex = 0;
            }
            
        }
        
        connection->_isClosing = true;
        shutdown(connection->_fd, SHUT_RD);
        connection->_canClose = true;
        
        printf("ERROR: Server::Sender::connectionRoutine -- routine end for fd %d\n", connection->_fd);
        
        return NULL;
    }
    
    bool Server::MultiThreadSender::start() {
        // 启动线程
        for (std::vector<ThreadData>::iterator it = _threadDatas.begin(); it != _threadDatas.end(); it++) {
            it->_queueContext._sender = this;
            it->_t = std::thread(threadEntry, &(*it));
        }
        
        return true;
    }
    
    void Server::MultiThreadSender::addConnection(std::shared_ptr<Connection> connection) {
        _lastThreadIndex = (_lastThreadIndex + 1) % _threadNum;
        
        connection->setSendThreadIndex(_lastThreadIndex);
        
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::INIT;
        senderTask->connection = connection;
        senderTask->rpcTask = NULL;
        
        _threadDatas[_lastThreadIndex]._queueContext._queue.push(senderTask);
    }
    
    void Server::MultiThreadSender::removeConnection(std::shared_ptr<Connection> connection) {
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::CLOSE;
        senderTask->connection = connection;
        senderTask->rpcTask = NULL;
        
        _threadDatas[connection->getSendThreadIndex()]._queueContext._queue.push(senderTask);
    }
    
    void Server::MultiThreadSender::postRpcTask(std::shared_ptr<Connection> connection, RpcTask *rpcTask) {
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::DATA;
        senderTask->connection = connection;
        senderTask->rpcTask = rpcTask;
        
        _threadDatas[connection->getSendThreadIndex()]._queueContext._queue.push(senderTask);
    }
    
    void Server::MultiThreadSender::threadEntry( ThreadData *tdata ) {
        // 启动send协程
        RoutineEnvironment::startCoroutine(taskQueueRoutine, &tdata->_queueContext);
        
        RoutineEnvironment::runEventLoop();
    }
    
    bool Server::CoroutineSender::start() {
        RoutineEnvironment::startCoroutine(taskQueueRoutine, &_queueContext);
        
        return true;
    }
    
    void Server::CoroutineSender::addConnection(std::shared_ptr<Connection> connection) {
        connection->setSendThreadIndex(0);
        
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::INIT;
        senderTask->connection = connection;
        senderTask->rpcTask = NULL;
        
        _queueContext._queue.push(senderTask);
    }
    
    void Server::CoroutineSender::removeConnection(std::shared_ptr<Connection> connection) {
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::CLOSE;
        senderTask->connection = connection;
        senderTask->rpcTask = NULL;
        
        _queueContext._queue.push(senderTask);
    }
    
    void Server::CoroutineSender::postRpcTask(std::shared_ptr<Connection> connection, RpcTask *rpcTask) {
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::DATA;
        senderTask->connection = connection;
        senderTask->rpcTask = rpcTask;
        
        _queueContext._queue.push(senderTask);
    }
    
    Server::Worker::~Worker() {
        
    }

    // pipe通知版本
    void *Server::Worker::taskHandleRoutine( void * arg ) {
        QueueContext *context = (QueueContext*)arg;
        
        WorkerTaskQueue& tqueue = context->_queue;
        Worker *self = context->_worker;
        Sender *sender = self->_server->getSender();
        
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
            WorkerTask *task = tqueue.pop();
            while (task) {
                hopeNum++;
                
                bool needCoroutine = task->rpcTask->method_descriptor->options().GetExtension(corpc::need_coroutine);
                
                if (needCoroutine) {
                    // 启动协程进行rpc处理
                    RoutineEnvironment::startCoroutine(taskCallRoutine, task);
                } else {
                    // rpc处理方法调用
                    task->rpcTask->service->CallMethod(task->rpcTask->method_descriptor, task->rpcTask->controller, task->rpcTask->request, task->rpcTask->response, NULL);
                    
                    // 处理结果发给sender处理
                    sender->postRpcTask(task->connection, task->rpcTask);
                    
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
        QueueContext *context = (QueueContext*)arg;
     
        WorkerTaskQueue& tqueue = context->_queue;
        Worker *self = context->_worker;
        Sender *sender = self->_server->getSender();
     
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
                    sender->postRpcTask(task->connection, task->rpcTask);
     
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
        task->connection->getSender()->postRpcTask(task->connection, task->rpcTask);
        
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
    
    Server::Server(bool acceptInNewThread, uint16_t receiveThreadNum, uint16_t sendThreadNum, uint16_t workThreadNum, const std::string& ip, uint16_t port): _acceptInNewThread(acceptInNewThread), _receiveThreadNum(receiveThreadNum), _sendThreadNum(sendThreadNum), _workThreadNum(workThreadNum), _ip(ip), _port(port) {
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
        if (_sendThreadNum == 0 && _receiveThreadNum == 0) {
            printf("ERROR: Server::start() -- sender and receiver can't run at same thread.\n");
            return false;
        }
        
        // 根据需要启动accept协程或线程
        if (_acceptInNewThread) {
            _acceptor = new ThreadAcceptor(this);
        } else {
            _acceptor = new CoroutineAcceptor(this);
        }
        
        // 根据需要启动receiver线程或协程
        if (_receiveThreadNum > 0) {
            _receiver = new MultiThreadReceiver(this, _receiveThreadNum);
        } else {
            _receiver = new CoroutineReceiver(this);
        }
        
        // 根据需要启动sender线程或协程
        if (_sendThreadNum > 0) {
            _sender = new MultiThreadSender(this, _sendThreadNum);
        } else {
            _sender = new CoroutineSender(this);
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
        
        if (!_sender->start()) {
            printf("ERROR: Server::start() -- start sender failed.\n");
            return false;
        }
        
        if (!_worker->start()) {
            printf("ERROR: Server::start() -- start worker failed.\n");
            return false;
        }
        
        return true;
    }

}
