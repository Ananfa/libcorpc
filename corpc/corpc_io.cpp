/*
 * Created by Xianke Liu on 2018/2/23.
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

#include "corpc_io.h"
#include "corpc_utils.h"

#include <sys/time.h>
#include <arpa/inet.h>
#include <fcntl.h>

// TODO: 使用统一的Log接口记录Log

namespace CoRpc {
    
    //Decoder::~Decoder() {}
    
    
    Worker::Worker::~Worker() {
        
    }
    
    // pipe通知版本
    void *Worker::msgHandleRoutine( void * arg ) {
        QueueContext *context = (QueueContext*)arg;
        
        WorkerMessageQueue& queue = context->_queue;
        Worker *self = context->_worker;
        
        // 初始化pipe readfd
        co_enable_hook_sys();
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
                    printf("ERROR: Worker::msgHandleRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                           readFd, ret, errno, strerror(errno));
                    
                    // TODO: 如何处理？退出协程？
                    // sleep 10 milisecond
                    msleep(10);
                }
            }
            
            struct timeval t1,t2;
            gettimeofday(&t1, NULL);
            
            // 处理任务队列
            void *msg = queue.pop();
            while (msg) {
                self->handleMessage(msg);
                
                // 防止其他协程（如：RoutineEnvironment::deamonRoutine）长时间不被调度，这里在处理一段时间后让出一下
                gettimeofday(&t2, NULL);
                if ((t2.tv_sec - t1.tv_sec) * 1000000 + t2.tv_usec - t1.tv_usec > 100000) {
                    msleep(1);
                
                    gettimeofday(&t1, NULL);
                }
                
                msg = queue.pop();
            }
        }
        
        return NULL;
    }
    
    MultiThreadWorker::~MultiThreadWorker() {}
    
    void MultiThreadWorker::threadEntry( ThreadData *tdata ) {
        // 启动rpc任务处理协程
        RoutineEnvironment::startCoroutine(msgHandleRoutine, &tdata->_queueContext);
        
        RoutineEnvironment::runEventLoop();
    }
    
    void MultiThreadWorker::start() {
        // 启动线程
        for (std::vector<ThreadData>::iterator it = _threadDatas.begin(); it != _threadDatas.end(); it++) {
            it->_queueContext._worker = this;
            it->_t = std::thread(threadEntry, &(*it));
        }
    }
    
    void MultiThreadWorker::addMessage(void *msg) {
        uint16_t index = (_lastThreadIndex++) % _threadNum;
        _threadDatas[index]._queueContext._queue.push(msg);
    }
    
    CoroutineWorker::~CoroutineWorker() {}
    
    void CoroutineWorker::start() {
        // 启动rpc任务处理协程
        RoutineEnvironment::startCoroutine(msgHandleRoutine, &_queueContext);
    }
    
    void CoroutineWorker::addMessage(void *msg) {
        _queueContext._queue.push(msg);
    }
    
    //Encoder::~Encoder() {}
    
    Pipeline::Pipeline(std::shared_ptr<Connection> &connection, DecodeFunction decodeFun, Worker *worker, std::vector<EncodeFunction> encodeFuns, uint headSize, uint maxBodySize): _connection(connection), _decodeFun(decodeFun), _worker(worker), _encodeFuns(std::move(encodeFuns)), _headSize(headSize), _maxBodySize(maxBodySize), _bodySize(0), _head(headSize,0), _body(maxBodySize,0) {
        _headBuf = (uint8_t *)_head.data();
        _bodyBuf = (uint8_t *)_body.data();
    }
    
    Pipeline::~Pipeline() {}
    
    bool Pipeline::downflow(uint8_t *buf, int space, int &size) {
        std::shared_ptr<Connection> connection = _connection.lock();
        assert(connection);
        
        size = 0;
        while (connection->_datas.size() > 0) {
            bool encoded = false;
            for (std::vector<EncodeFunction>::iterator it = _encodeFuns.begin(); it != _encodeFuns.end(); it++) {
                int tmp = 0;
                if ((*it)(connection, connection->_datas.front(), buf + size, space - size, tmp)) {
                    if (!tmp) { // 数据放不进buf中（等下次放入）
                        return true;
                    }
                    
                    size += tmp;
                    encoded = true;
                    break;
                }
            }
            
            if (!encoded) {
                // 找不到可处理的编码器
                return false;
            }
            
            connection->_datas.pop_front();
        }
        
        return true;
    }
    
    TcpPipeline::TcpPipeline(std::shared_ptr<Connection> &connection, DecodeFunction decodeFun, Worker *worker, std::vector<EncodeFunction> encodeFuns, uint headSize, uint maxBodySize, uint bodySizeOffset, SIZE_TYPE bodySizeType): CoRpc::Pipeline(connection, decodeFun, worker, std::move(encodeFuns), headSize, maxBodySize), _bodySizeOffset(bodySizeOffset), _bodySizeType(bodySizeType), _headNum(0), _bodyNum(0) {
    }
    
    bool TcpPipeline::upflow(uint8_t *buf, int size) {
        std::shared_ptr<Connection> connection = _connection.lock();
        assert(connection);
        
        // 解析数据
        int offset = 0;
        while (size > offset) {
            // 先解析头部
            if (_headNum < _headSize) {
                int needNum = _headSize - _headNum;
                if (size - offset > needNum) {
                    memcpy(_headBuf + _headNum, buf + offset, needNum);
                    _headNum = _headSize;
                    
                    offset += needNum;
                } else {
                    memcpy(_headBuf + _headNum, buf + offset, size - offset);
                    _headNum += size - offset;
                    
                    break;
                }
            }
            
            if (!_bodySize) {
                // 解析消息长度值
                if (_bodySizeType == TWO_BYTES) {
                    uint16_t x = *(uint16_t*)(_headBuf + _bodySizeOffset);
                    _bodySize = ntohs(x);
                } else {
                    assert(_bodySizeType == FOUR_BYTES);
                    uint32_t x = *(uint32_t*)(_headBuf + _bodySizeOffset);
                    _bodySize = ntohl(x);
                }
                
                if (_bodySize > _maxBodySize) { // 数据超长
                    printf("ERROR: TcpPipeline::upflow -- request too large in thread\n");
                    
                    return false;
                }
            }
            
            // 从缓存中解析数据
            if (_bodyNum < _bodySize) {
                int needNum = _bodySize - _bodyNum;
                if (size - offset >= needNum) {
                    memcpy(_bodyBuf + _bodyNum, buf + offset, needNum);
                    _bodyNum = _bodySize;
                    
                    offset += needNum;
                } else {
                    memcpy(_bodyBuf + _bodyNum, buf + offset, size - offset);
                    _bodyNum += size - offset;
                    
                    break;
                }
            }
            
            void *msg = _decodeFun(connection, _headBuf, _bodyBuf, _bodySize);
            
            if (!msg) {
                return false;
            }
            
            _worker->addMessage(msg);
            
            // 处理完一个请求消息，复位状态
            _headNum = 0;
            _bodyNum = 0;
            _bodySize = 0;
        }
        
        return true;
    }
    
    UdpPipeline::UdpPipeline(std::shared_ptr<Connection> &connection, DecodeFunction decodeFun, Worker *worker, std::vector<EncodeFunction> encodeFuns, uint headSize, uint maxBodySize): CoRpc::Pipeline(connection, decodeFun, worker, std::move(encodeFuns), headSize, maxBodySize) {
        
    }
    
    bool UdpPipeline::upflow(uint8_t *buf, int size) {
        std::shared_ptr<Connection> connection = _connection.lock();
        assert(connection);
        
        if (size < _headSize) {
            printf("ERROR: UdpPipeline::upflow -- package size too small\n");
            
            return false;
        }
        
        memcpy(_headBuf, buf, _headSize);
        
        _bodySize = size - _headSize;
        
        memcpy(_bodyBuf, buf + _headSize, _bodySize);
        
        void *msg = _decodeFun(connection, _headBuf, _bodyBuf, _bodySize);
        
        if (!msg) {
            return false;
        }
        
        _worker->addMessage(msg);
        
        return true;
    }
    
    PipelineFactory::~PipelineFactory() {}
    
    std::shared_ptr<CoRpc::Pipeline> TcpPipelineFactory::buildPipeline(std::shared_ptr<CoRpc::Connection> &connection) {
        return std::shared_ptr<CoRpc::Pipeline>( new CoRpc::TcpPipeline(connection, _decodeFun, _worker, _encodeFuns, _headSize, _maxBodySize, _bodySizeOffset, _bodySizeType) );
    }
    
    std::shared_ptr<CoRpc::Pipeline> UdpPipelineFactory::buildPipeline(std::shared_ptr<CoRpc::Connection> &connection) {
        return std::shared_ptr<CoRpc::Pipeline>( new CoRpc::UdpPipeline(connection, _decodeFun, _worker, _encodeFuns, _headSize, _maxBodySize) );
    }
    
    Connection::Connection(int fd, IO* io): _fd(fd), _io(io), _routineHang(false), _routine(NULL), _sendThreadIndex(-1), _recvThreadIndex(-1), _isClosing(false), _canClose(false) {
    }
    
    Connection::~Connection() {
        
    }
    
    void Connection::send(std::shared_ptr<void> data) {
        std::shared_ptr<Connection> self = shared_from_this();
        _io->_sender->send(self, data);
    }
    
    Server::~Server() {}
    
    void Server::buildAndAddConnection(int fd) {
        std::shared_ptr<CoRpc::Connection> connection(buildConnection(fd));
        std::shared_ptr<CoRpc::Pipeline> pipeline = getPipelineFactory()->buildPipeline(connection);
        connection->setPipeline(pipeline);
        
        // 将接受的连接分别发给Receiver和Sender
        _io->addConnection(connection);
        
        // TODO: 产生连接消息
        
    }
    
    
    Acceptor::~Acceptor() {
        
    }
    
    bool Acceptor::init() {
        _listen_fd = socket(AF_INET,SOCK_STREAM, IPPROTO_TCP);
        if( _listen_fd >= 0 )
        {
            if(_port != 0)
            {
                int nReuseAddr = 1;
                setsockopt(_listen_fd,SOL_SOCKET,SO_REUSEADDR,&nReuseAddr,sizeof(nReuseAddr));
                
                struct sockaddr_in addr ;
                bzero(&addr,sizeof(addr));
                addr.sin_family = AF_INET;
                addr.sin_port = htons(_port);
                int nIP = 0;
                
                if (_ip.empty() ||
                    _ip.compare("0") == 0 ||
                    _ip.compare("0.0.0.0") == 0 ||
                    _ip.compare("*") == 0) {
                    nIP = htonl(INADDR_ANY);
                }
                else
                {
                    nIP = inet_addr(_ip.c_str());
                }
                addr.sin_addr.s_addr = nIP;
                
                int ret = bind(_listen_fd,(struct sockaddr*)&addr,sizeof(addr));
                if( ret != 0)
                {
                    close(_listen_fd);
                    _listen_fd = -1;
                }
            } else {
                close(_listen_fd);
                _listen_fd = -1;
            }
        }
        
        if(_listen_fd==-1){
            printf("ERROR: Acceptor::init() -- Port %d is in use\n", _port);
            return false;
        }
        
        printf("INFO: Acceptor::init() -- listen %d %s:%d\n", _listen_fd, _ip.c_str(), _port);
        listen( _listen_fd, 1024 );
        
        int iFlags;
        iFlags = fcntl(_listen_fd, F_GETFL, 0);
        iFlags |= O_NONBLOCK;
        iFlags |= O_NDELAY;
        fcntl(_listen_fd, F_SETFL, iFlags);
        
        return true;
    }
    
    void *Acceptor::acceptRoutine( void * arg ) {
        Acceptor *self = (Acceptor *)arg;
        co_enable_hook_sys();
        
        Server *server = self->_server;
        
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
            
            server->buildAndAddConnection(fd);
        }
        
        return NULL;
    }
    
    bool Acceptor::start() {
        if (!init()) {
            return false;
        }
        
        RoutineEnvironment::startCoroutine(acceptRoutine, this);
        
        return true;
    }
    
    Receiver::~Receiver() {}
    
    void *Receiver::connectionDispatchRoutine( void * arg ) {
        QueueContext *context = (QueueContext*)arg;
        
        ReceiverTaskQueue& queue = context->_queue;
        
        // 初始化pipe readfd
        co_enable_hook_sys();
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
                    printf("ERROR: Receiver::connectDispatchRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                           readFd, ret, errno, strerror(errno));
                    
                    // TODO: 如何处理？退出协程？
                    // sleep 10 milisecond
                    msleep(10);
                }
            }
            
            // 处理任务队列
            ReceiverTask* recvTask = queue.pop();
            while (recvTask) {
                RoutineEnvironment::startCoroutine(connectionRoutine, recvTask);
                
                recvTask = queue.pop();
            }
        }
    }
    
    void *Receiver::connectionRoutine( void * arg ) {
        co_enable_hook_sys();
        ReceiverTask *recvTask = (ReceiverTask *)arg;
        std::shared_ptr<Connection> connection = recvTask->connection;
        delete recvTask;
        
        IO *io = connection->_io;
        
        int fd = connection->getfd();
        printf("INFO: start connectionRoutine for fd:%d in thread:%d\n", fd, GetPid());
        
        std::string buffs(CORPC_MAX_BUFFER_SIZE,0);
        uint8_t *buf = (uint8_t *)buffs.data();
        
        while (true) {
            // 先将数据读到缓存中（尽可能多的读）
            int ret = (int)read(fd, buf, CORPC_MAX_BUFFER_SIZE);
            
            if (ret <= 0) {
                // ret 0 mean disconnected
                if (ret < 0 && errno == EAGAIN) {
                    continue;
                }
                
                // 出错处理
                printf("ERROR: Receiver::connectionRoutine -- read reqhead fd %d ret %d errno %d (%s)\n",
                       fd, ret, errno, strerror(errno));
                
                break;
            }
            
            if (!connection->getPipeline()->upflow(buf, ret)) {
                break;
            }
        }
       
        io->_sender->removeConnection(connection); // 通知sender关闭connection
        shutdown(fd, SHUT_WR);  // 让sender中的fd相关协程退出
        
        // 等待写关闭
        while (!connection->_canClose) {
            // sleep 100 milisecond
            msleep(100);
        }
        
        close(fd);
        
        connection->onClose();
        
        return NULL;
    }
    
    void MultiThreadReceiver::threadEntry(ThreadData *tdata) {
        // 启动处理待处理连接协程
        RoutineEnvironment::startCoroutine(connectionDispatchRoutine, &tdata->_queueContext);
        
        RoutineEnvironment::runEventLoop();
    }
    
    bool MultiThreadReceiver::start() {
        // 启动线程
        for (std::vector<ThreadData>::iterator it = _threadDatas.begin(); it != _threadDatas.end(); it++) {
            it->_queueContext._receiver = this;
            it->_t = std::thread(threadEntry, &(*it));
        }
        
        return true;
    }
    
    void MultiThreadReceiver::addConnection(std::shared_ptr<Connection>& connection) {
        _lastThreadIndex = (_lastThreadIndex + 1) % _threadNum;
        
        connection->setRecvThreadIndex(_lastThreadIndex);
        
        ReceiverTask *recvTask = new ReceiverTask;
        recvTask->connection = connection;
        
        _threadDatas[_lastThreadIndex]._queueContext._queue.push(recvTask);
    }
    
    bool CoroutineReceiver::start() {
        RoutineEnvironment::startCoroutine(connectionDispatchRoutine, &_queueContext);
        
        return true;
    }
    
    void CoroutineReceiver::addConnection(std::shared_ptr<Connection>& connection) {
        connection->setRecvThreadIndex(0);
        
        ReceiverTask *recvTask = new ReceiverTask;
        recvTask->connection = connection;
        
        _queueContext._queue.push(recvTask);
    }
    
    Sender::~Sender() {
        
    }
    
    void *Sender::taskQueueRoutine( void * arg ) {
        QueueContext *context = (QueueContext*)arg;
        
        SenderTaskQueue& queue = context->_queue;
        
        // 初始化pipe readfd
        co_enable_hook_sys();
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
                    printf("ERROR: Sender::taskQueueRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                           readFd, ret, errno, strerror(errno));
                    
                    // TODO: 如何处理？退出协程？
                    // sleep 10 milisecond
                    msleep(10);
                }
            }
        
            // 处理任务队列
            SenderTask *task = queue.pop();
            while (task) {
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
                        if (!task->connection->_isClosing) {
                            task->connection->_datas.push_back(task->data);
                            
                            // 唤醒挂起的connection协程
                            if (task->connection->_routineHang) {
                                co_resume(task->connection->_routine);
                            }
                        } else {
                            task->connection->cleanDataOnClosing(task->data);
                        }
                        
                        delete task;
                        break;
                }
                
                task = queue.pop();
            }
        }
        
        return NULL;
    }
    
    void *Sender::connectionRoutine( void * arg ) {
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
            
            int tmp = 0;
            if (!connection->getPipeline()->downflow(buf + endIndex, CORPC_MAX_BUFFER_SIZE - endIndex, tmp)) {
                break;
            }
            
            endIndex += tmp;
            
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
            int ret = (int)write(connection->_fd, buf + startIndex, dataSize);
            if (ret < 0) {
                printf("ERROR: Sender::connectionRoutine -- write resphead fd %d ret %d errno %d (%s)\n",
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
        
        printf("ERROR: Sender::connectionRoutine -- routine end for fd %d\n", connection->_fd);
        
        return NULL;
    }
    
    bool MultiThreadSender::start() {
        // 启动线程
        for (std::vector<ThreadData>::iterator it = _threadDatas.begin(); it != _threadDatas.end(); it++) {
            it->_queueContext._sender = this;
            it->_t = std::thread(threadEntry, &(*it));
        }
        
        return true;
    }
    
    void MultiThreadSender::addConnection(std::shared_ptr<Connection>& connection) {
        _lastThreadIndex = (_lastThreadIndex + 1) % _threadNum;
        
        connection->setSendThreadIndex(_lastThreadIndex);
        
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::INIT;
        senderTask->connection = connection;
        
        _threadDatas[_lastThreadIndex]._queueContext._queue.push(senderTask);
    }
    
    void MultiThreadSender::removeConnection(std::shared_ptr<Connection>& connection) {
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::CLOSE;
        senderTask->connection = connection;
        
        _threadDatas[connection->getSendThreadIndex()]._queueContext._queue.push(senderTask);
    }
    
    void MultiThreadSender::send(std::shared_ptr<Connection>& connection, std::shared_ptr<void> data) {
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::DATA;
        senderTask->connection = connection;
        senderTask->data = data;
        
        _threadDatas[connection->getSendThreadIndex()]._queueContext._queue.push(senderTask);
    }
    
    void MultiThreadSender::threadEntry( ThreadData *tdata ) {
        // 启动send协程
        RoutineEnvironment::startCoroutine(taskQueueRoutine, &tdata->_queueContext);
        
        RoutineEnvironment::runEventLoop();
    }
    
    bool CoroutineSender::start() {
        RoutineEnvironment::startCoroutine(taskQueueRoutine, &_queueContext);
        
        return true;
    }
    
    void CoroutineSender::addConnection(std::shared_ptr<Connection>& connection) {
        connection->setSendThreadIndex(0);
        
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::INIT;
        senderTask->connection = connection;
        
        _queueContext._queue.push(senderTask);
    }
    
    void CoroutineSender::removeConnection(std::shared_ptr<Connection>& connection) {
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::CLOSE;
        senderTask->connection = connection;
        
        _queueContext._queue.push(senderTask);
    }
    
    void CoroutineSender::send(std::shared_ptr<Connection>& connection, std::shared_ptr<void> data) {
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::DATA;
        senderTask->connection = connection;
        senderTask->data = data;
        
        _queueContext._queue.push(senderTask);
    }
    
    IO::IO(uint16_t receiveThreadNum, uint16_t sendThreadNum): _receiveThreadNum(receiveThreadNum), _sendThreadNum(sendThreadNum) {
    }
    
    IO* IO::create(uint16_t receiveThreadNum, uint16_t sendThreadNum) {
        if (receiveThreadNum == 0 && sendThreadNum == 0) {
            printf("ERROR: IO::create() -- sender and receiver can't run at same thread.\n");
            return nullptr;
        }
        
        IO *io = new IO(receiveThreadNum, sendThreadNum);
        io->start();
        
        return io;
    }
    
    bool IO::start() {
        if (_sendThreadNum == 0 && _receiveThreadNum == 0) {
            printf("ERROR: IO::start() -- sender and receiver can't run at same thread.\n");
            return false;
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
        
        if (!_receiver->start()) {
            printf("ERROR: IO::start() -- start receiver failed.\n");
            return false;
        }
        
        if (!_sender->start()) {
            printf("ERROR: IO::start() -- start sender failed.\n");
            return false;
        }
        
        return true;
    }

    void IO::addConnection(std::shared_ptr<Connection>& connection) {
        _receiver->addConnection(connection);
        _sender->addConnection(connection);
    }
    
    void IO::removeConnection(std::shared_ptr<Connection>& connection) {
        _sender->removeConnection(connection);
    }
    
}
