/*
 * Created by Xianke Liu on 2017/11/21.
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
#include "corpc_rpc_server.h"
#include "corpc_controller.h"
#include "corpc_utils.h"

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
    RpcServer::Decoder::~Decoder() {}
    
    void * RpcServer::Decoder::decode(std::shared_ptr<CoRpc::Connection> &connection, uint8_t *head, uint8_t *body, int size) {
        std::shared_ptr<Connection> conn = std::static_pointer_cast<Connection>(connection);
        
        RpcServer *server = conn->getServer();
        
        uint32_t reqSize = *(uint32_t *)head;
        reqSize = ntohl(reqSize);
        uint32_t serviceId = *(uint32_t *)(head + 4);
        serviceId = ntohl(serviceId);
        uint32_t methodId = *(uint32_t *)(head + 8);
        methodId = ntohl(methodId);
        uint64_t callId = *(uint64_t *)(head + 12);
        callId = ntohll(callId);
        
        // 生成ServerRpcTask
        // 根据serverId和methodId查表
        const MethodData *methodData = server->getMethod(serviceId, methodId);
        if (methodData != NULL) {
            const google::protobuf::MethodDescriptor *method_descriptor = methodData->method_descriptor;
            
            google::protobuf::Message *request = methodData->request_proto->New();
            google::protobuf::Message *response = method_descriptor->options().GetExtension(corpc::not_care_response) ? NULL : methodData->response_proto->New();
            Controller *controller = new Controller();
            if (!request->ParseFromArray(body, size)) {
                // 出错处理
                printf("ERROR: Server::Decoder::decode -- parse request body fail\n");
                
                return nullptr;
            }
            
            // 将收到的请求传给worker
            WorkerTask *task = new WorkerTask;
            task->connection = conn;
            task->rpcTask = std::shared_ptr<RpcTask>(new RpcTask);
            task->rpcTask->service = server->getService(serviceId);
            task->rpcTask->method_descriptor = method_descriptor;
            task->rpcTask->request = request;
            task->rpcTask->response = response;
            task->rpcTask->controller = controller;
            task->rpcTask->callId = callId;
            
            return task;
        } else {
            // 出错处理
            printf("ERROR: Server::Decoder::decode -- can't find method object of serviceId: %u methodId: %u\n", serviceId, methodId);
            
            return nullptr;
        }
    }
    
    RpcServer::Router::~Router() {}
    
    void RpcServer::Router::route(std::shared_ptr<CoRpc::Connection> &connection, void *msg) {
        std::shared_ptr<Connection> conn = std::static_pointer_cast<Connection>(connection);
        
        RpcServer *server = conn->getServer();
        
        WorkerTask *task = (WorkerTask *)msg;
        server->_worker->postRpcTask(task);
    }
    
    RpcServer::Encoder::~Encoder() {}
    
    bool RpcServer::Encoder::encode(std::shared_ptr<CoRpc::Connection> &connection, std::shared_ptr<void>& data, uint8_t *buf, int space, int &size) {
        std::shared_ptr<RpcTask> rpcTask = std::static_pointer_cast<RpcTask>(data);
        uint32_t msgSize = rpcTask->response->GetCachedSize();
        if (msgSize == 0) {
            msgSize = rpcTask->response->ByteSize();
        }
        
        if (msgSize + CORPC_RESPONSE_HEAD_SIZE >= space) {
            return true;
        }
        
        *(uint32_t *)buf = htonl(msgSize);
        *(uint64_t *)(buf + 4) = htonll(rpcTask->callId);
        
        rpcTask->response->SerializeWithCachedSizesToArray(buf + CORPC_RESPONSE_HEAD_SIZE);
        size = CORPC_RESPONSE_HEAD_SIZE + msgSize;
        
        return true;
    }
    
    std::shared_ptr<CoRpc::Pipeline> RpcServer::PipelineFactory::buildPipeline(std::shared_ptr<CoRpc::Connection> &connection) {
        std::shared_ptr<CoRpc::Pipeline> pipeline( new CoRpc::TcpPipeline(connection, CORPC_REQUEST_HEAD_SIZE, CORPC_MAX_REQUEST_SIZE, 0, CoRpc::Pipeline::FOUR_BYTES) );
        
        pipeline->setDecoder(_decoder);
        pipeline->setRouter(_router);
        pipeline->addEncoder(_encoder);
        
        return pipeline;
    }
    
    RpcServer::Connection::Connection(int fd, RpcServer* server): CoRpc::Connection(fd, server->_io), _server(server) {
    }
    
    RpcServer::Connection::~Connection() {
        printf("INFO: Server::Connection::~Connection -- fd:%d in thread:%d\n", _fd, GetPid());
    }
    
    RpcServer::RpcTask::RpcTask(): service(NULL), method_descriptor(NULL), request(NULL), response(NULL), controller(NULL) {
        
    }
    
    RpcServer::RpcTask::~RpcTask() {
        delete request;
        delete response;
        delete controller;
    }
    
    RpcServer::Acceptor::~Acceptor() {
        
    }
    
    bool RpcServer::Acceptor::init() {
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
    
    void *RpcServer::Acceptor::acceptRoutine( void * arg ) {
        Acceptor *self = (Acceptor *)arg;
        co_enable_hook_sys();
        
        IO *io = self->_server->_io;
        
        int listen_fd = self->_listen_fd;
        // 侦听连接，并把接受的连接传给连接处理对象
        printf("INFO: start accept from listen fd %d\n", listen_fd);
        for(;;)
        {
            struct sockaddr_in addr; //maybe sockaddr_un;
            memset( &addr,0,sizeof(addr) );
            socklen_t len = sizeof(addr);
            
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
            // 保持连接
            setKeepAlive(fd, 10);
            
            // 设置读写超时时间，默认为1秒
            co_set_timeout(fd, -1, 1000);
            
            std::shared_ptr<CoRpc::Connection> connection(new Connection(fd, self->_server));
            std::shared_ptr<CoRpc::Pipeline> pipeline = PipelineFactory::Instance().buildPipeline(connection);
            connection->setPipeline(pipeline);
            
            // 将接受的连接分别发给Receiver和Sender
            io->addConnection(connection);
        }
        
        return NULL;
    }
    
    void RpcServer::ThreadAcceptor::threadEntry(ThreadAcceptor *self) {
        if (!self->init()) {
            printf("ERROR: ThreadAcceptor::threadEntry() -- init fail\n");
            return;
        }
        
        // 启动accept协程
        RoutineEnvironment::startCoroutine(acceptRoutine, self);
        
        RoutineEnvironment::runEventLoop();
    }
    
    bool RpcServer::ThreadAcceptor::start() {
        _t = std::thread(threadEntry, this);
        return true;
    }
    
    bool RpcServer::CoroutineAcceptor::start() {
        if (!init()) {
            return false;
        }
        
        RoutineEnvironment::startCoroutine(acceptRoutine, this);
        
        return true;
    }
    
    RpcServer::Worker::~Worker() {
        
    }

    // pipe通知版本
    void *RpcServer::Worker::taskHandleRoutine( void * arg ) {
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
                    msleep(10);
                }
            }
            
            struct timeval t1,t2;
            gettimeofday(&t1, NULL);
            
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
                    
                    if (task->rpcTask->response != NULL) {
                        // 处理结果发给sender处理
                        io->getSender()->send(task->connection, task->rpcTask);
                    }
                    
                    delete task;
                }
                
                // 防止其他协程（如：RoutineEnvironment::deamonRoutine）长时间不被调度，这里在处理一段时间后让出一下
                gettimeofday(&t2, NULL);
                if ((t2.tv_sec - t1.tv_sec) * 1000000 + t2.tv_usec - t1.tv_usec > 100000) {
                    msleep(1);
                    
                    gettimeofday(&t1, NULL);
                }
                
                task = tqueue.pop();
            }
        }
        
        return NULL;
    }

    /*
    // 轮询版本
    void *RpcServer::Worker::taskHandleRoutine( void * arg ) {
        QueueContext *context = (QueueContext*)arg;
     
        WorkerTaskQueue& tqueue = context->_queue;
        Worker *self = context->_worker;
        IO *io = self->_server->getIO();
     
        // 初始化pipe readfd
        co_enable_hook_sys();

        while (true) {
            usleep(4000);
            
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
    
    void *RpcServer::Worker::taskCallRoutine( void * arg ) {
        WorkerTask *task = (WorkerTask *)arg;
        
        task->rpcTask->service->CallMethod(task->rpcTask->method_descriptor, task->rpcTask->controller, task->rpcTask->request, task->rpcTask->response, NULL);
        
        if (task->rpcTask->response != NULL) {
            // 处理结果发给sender处理
            task->connection->send(task->rpcTask);
        }
        
        delete task;
        
        return NULL;
    }
    
    void RpcServer::MultiThreadWorker::threadEntry( ThreadData *tdata ) {
        // 启动rpc任务处理协程
        RoutineEnvironment::startCoroutine(taskHandleRoutine, &tdata->_queueContext);
        
        RoutineEnvironment::runEventLoop();
    }
    
    bool RpcServer::MultiThreadWorker::start() {
        // 启动线程
        for (std::vector<ThreadData>::iterator it = _threadDatas.begin(); it != _threadDatas.end(); it++) {
            it->_queueContext._worker = this;
            it->_t = std::thread(threadEntry, &(*it));
        }
        
        return true;
    }
    
    void RpcServer::MultiThreadWorker::postRpcTask(WorkerTask *task) {
        uint16_t index = (_lastThreadIndex + 1) % _threadNum;
        // FIXME: 对_lastThreadIndex的处理是否需要加锁？
        _lastThreadIndex = index;
        _threadDatas[index]._queueContext._queue.push(task);
    }
    
    bool RpcServer::CoroutineWorker::start() {
        // 启动rpc任务处理协程
        RoutineEnvironment::startCoroutine(taskHandleRoutine, &_queueContext);
        
        return true;
    }
    
    void RpcServer::CoroutineWorker::postRpcTask(WorkerTask *task) {
        _queueContext._queue.push(task);
    }
    
    RpcServer::RpcServer(IO *io, bool acceptInNewThread, uint16_t workThreadNum, const std::string& ip, uint16_t port): _io(io), _acceptInNewThread(acceptInNewThread), _workThreadNum(workThreadNum), _ip(ip), _port(port) {
    }
    
    RpcServer* RpcServer::create(IO *io, bool acceptInNewThread, uint16_t workThreadNum, const std::string& ip, uint16_t port) {
        assert(io);
        RpcServer *server = new RpcServer(io, acceptInNewThread, workThreadNum, ip, port);
        
        server->start();
        return server;
    }
    
    bool RpcServer::registerService(::google::protobuf::Service *rpcService) {
        const google::protobuf::ServiceDescriptor *serviceDescriptor = rpcService->GetDescriptor();
        
        uint32_t serviceId = (uint32_t)(serviceDescriptor->options().GetExtension(corpc::global_service_id));
        
        std::map<uint32_t, ServiceData>::iterator it = _services.find(serviceId);
        if (it != _services.end()) {
            return false;
        }
        
        ServiceData &serviceData = _services[serviceId];
        serviceData.rpcService = rpcService;
        
        MethodData methodData;
        for (int i = 0; i < serviceDescriptor->method_count(); i++) {
            methodData.method_descriptor = serviceDescriptor->method(i);
            methodData.request_proto = &rpcService->GetRequestPrototype(methodData.method_descriptor);
            methodData.response_proto= &rpcService->GetResponsePrototype(methodData.method_descriptor);
            
            serviceData.methods.push_back(methodData);
        }
        
        return true;
    }
    
    google::protobuf::Service *RpcServer::getService(uint32_t serviceId) const {
        std::map<uint32_t, ServiceData>::const_iterator it = _services.find(serviceId);
        if (it == _services.end()) {
            return NULL;
        }
        
        return it->second.rpcService;
    }
    
    const MethodData *RpcServer::getMethod(uint32_t serviceId, uint32_t methodId) const {
        std::map<uint32_t, ServiceData>::const_iterator it = _services.find(serviceId);
        if (it == _services.end()) {
            return NULL;
        }
        
        if (it->second.methods.size() <= methodId) {
            return NULL;
        }
        
        return &(it->second.methods[methodId]);
    }
    
    bool RpcServer::start() {
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
