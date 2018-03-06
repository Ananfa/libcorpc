//
//  corpc_io.cpp
//  rpcsvr
//
//  Created by Xianke Liu on 2018/2/23.
//Copyright © 2018年 Dena. All rights reserved.
//
#include "corpc_routine_env.h"

#include "corpc_io.h"

// TODO: 使用统一的Log接口记录Log

namespace CoRpc {
    IO::Connection::Connection(int fd, IO* io): _fd(fd), _io(io), _routineHang(false), _routine(NULL), _isClosing(false), _canClose(false) {
    }
    
    IO::Connection::~Connection() {
        
    }
    
    void IO::Connection::send(std::shared_ptr<void> data) {
        std::shared_ptr<Connection> self = shared_from_this();
        _io->_sender->send(self, data);
    }
    
    IO::Receiver::~Receiver() {
        
    }
    
    void *IO::Receiver::connectionDispatchRoutine( void * arg ) {
        QueueContext *context = (QueueContext*)arg;
        
        ReceiverTaskQueue& queue = context->_queue;
        Receiver *receiver = context->_receiver;
        
        // 初始化pipe readfd
        co_enable_hook_sys();
        int readFd = queue.getReadFd();
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
                    printf("ERROR: IO::Receiver::connectDispatchRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                           readFd, ret, errno, strerror(errno));
                    
                    // TODO: 如何处理？退出协程？
                    // sleep 10 milisecond
                    usleep(10000);
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
    
    void *IO::Receiver::connectionRoutine( void * arg ) {
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
            int ret = read(fd, buf, CORPC_MAX_BUFFER_SIZE);
            
            if (ret <= 0) {
                // ret 0 mean disconnected
                if (ret < 0 && errno == EAGAIN) {
                    continue;
                }
                
                // 出错处理
                printf("ERROR: IO::Receiver::connectionRoutine -- read reqhead fd %d ret %d errno %d (%s)\n",
                       fd, ret, errno, strerror(errno));
                
                break;
            }
            
            if (!connection->parseData(buf, ret)) {
                break;
            }
        }
       
        io->_sender->removeConnection(connection); // 通知sender关闭connection
        shutdown(fd, SHUT_WR);  // 让sender中的fd相关协程退出
        
        // 等待写关闭
        while (!connection->_canClose) {
            // sleep 100 milisecond
            usleep(100000);
        }
        
        close(fd);
        printf("INFO: IO::Receiver::connectionRoutine -- end for fd:%d in thread:%d\n", fd, GetPid());
        
        connection->onClose();
        
        return NULL;
    }
    
    void IO::MultiThreadReceiver::threadEntry(ThreadData *tdata) {
        // 启动处理待处理连接协程
        RoutineEnvironment::startCoroutine(connectionDispatchRoutine, &tdata->_queueContext);
        
        RoutineEnvironment::runEventLoop();
    }
    
    bool IO::MultiThreadReceiver::start() {
        // 启动线程
        for (std::vector<ThreadData>::iterator it = _threadDatas.begin(); it != _threadDatas.end(); it++) {
            it->_queueContext._receiver = this;
            it->_t = std::thread(threadEntry, &(*it));
        }
        
        return true;
    }
    
    void IO::MultiThreadReceiver::addConnection(std::shared_ptr<Connection>& connection) {
        _lastThreadIndex = (_lastThreadIndex + 1) % _threadNum;
        
        connection->setRecvThreadIndex(_lastThreadIndex);
        
        ReceiverTask *recvTask = new ReceiverTask;
        recvTask->connection = connection;
        
        _threadDatas[_lastThreadIndex]._queueContext._queue.push(recvTask);
    }
    
    bool IO::CoroutineReceiver::start() {
        RoutineEnvironment::startCoroutine(connectionDispatchRoutine, &_queueContext);
        
        return true;
    }
    
    void IO::CoroutineReceiver::addConnection(std::shared_ptr<Connection>& connection) {
        connection->setRecvThreadIndex(0);
        
        ReceiverTask *recvTask = new ReceiverTask;
        recvTask->connection = connection;
        
        _queueContext._queue.push(recvTask);
    }
    
    IO::Sender::~Sender() {
        
    }
    
    void *IO::Sender::taskQueueRoutine( void * arg ) {
        QueueContext *context = (QueueContext*)arg;
        
        SenderTaskQueue& queue = context->_queue;
        Sender *sender = context->_sender;
        
        // 初始化pipe readfd
        co_enable_hook_sys();
        int readFd = queue.getReadFd();
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
                    printf("ERROR: IO::Sender::taskQueueRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                           readFd, ret, errno, strerror(errno));
                    
                    // TODO: 如何处理？退出协程？
                    // sleep 10 milisecond
                    usleep(10000);
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
                        }
                        
                        delete task;
                        break;
                }
                
                task = queue.pop();
            }
        }
        
        return NULL;
    }
    
    void *IO::Sender::connectionRoutine( void * arg ) {
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
            
            endIndex += connection->buildData(buf + endIndex, CORPC_MAX_BUFFER_SIZE - endIndex);
            
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
                printf("ERROR: IO::Sender::connectionRoutine -- write resphead fd %d ret %d errno %d (%s)\n",
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
    
    bool IO::MultiThreadSender::start() {
        // 启动线程
        for (std::vector<ThreadData>::iterator it = _threadDatas.begin(); it != _threadDatas.end(); it++) {
            it->_queueContext._sender = this;
            it->_t = std::thread(threadEntry, &(*it));
        }
        
        return true;
    }
    
    void IO::MultiThreadSender::addConnection(std::shared_ptr<Connection>& connection) {
        _lastThreadIndex = (_lastThreadIndex + 1) % _threadNum;
        
        connection->setSendThreadIndex(_lastThreadIndex);
        
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::INIT;
        senderTask->connection = connection;
        
        _threadDatas[_lastThreadIndex]._queueContext._queue.push(senderTask);
    }
    
    void IO::MultiThreadSender::removeConnection(std::shared_ptr<Connection>& connection) {
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::CLOSE;
        senderTask->connection = connection;
        
        _threadDatas[connection->getSendThreadIndex()]._queueContext._queue.push(senderTask);
    }
    
    void IO::MultiThreadSender::send(std::shared_ptr<Connection>& connection, std::shared_ptr<void> data) {
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::DATA;
        senderTask->connection = connection;
        senderTask->data = data;
        
        _threadDatas[connection->getSendThreadIndex()]._queueContext._queue.push(senderTask);
    }
    
    void IO::MultiThreadSender::threadEntry( ThreadData *tdata ) {
        // 启动send协程
        RoutineEnvironment::startCoroutine(taskQueueRoutine, &tdata->_queueContext);
        
        RoutineEnvironment::runEventLoop();
    }
    
    bool IO::CoroutineSender::start() {
        RoutineEnvironment::startCoroutine(taskQueueRoutine, &_queueContext);
        
        return true;
    }
    
    void IO::CoroutineSender::addConnection(std::shared_ptr<Connection>& connection) {
        connection->setSendThreadIndex(0);
        
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::INIT;
        senderTask->connection = connection;
        
        _queueContext._queue.push(senderTask);
    }
    
    void IO::CoroutineSender::removeConnection(std::shared_ptr<Connection>& connection) {
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::CLOSE;
        senderTask->connection = connection;
        
        _queueContext._queue.push(senderTask);
    }
    
    void IO::CoroutineSender::send(std::shared_ptr<Connection>& connection, std::shared_ptr<void> data) {
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::DATA;
        senderTask->connection = connection;
        senderTask->data = data;
        
        _queueContext._queue.push(senderTask);
    }
    
    IO::IO(uint16_t receiveThreadNum, uint16_t sendThreadNum): _receiveThreadNum(receiveThreadNum), _sendThreadNum(sendThreadNum) {
    }
    
    IO *IO::create(uint16_t receiveThreadNum, uint16_t sendThreadNum) {
        if (receiveThreadNum == 0 && sendThreadNum == 0) {
            printf("ERROR: IO::start() -- sender and receiver can't run at same thread.\n");
            return NULL;
        }
        
        return new IO(receiveThreadNum, sendThreadNum);
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
            printf("ERROR: Server::start() -- start receiver failed.\n");
            return false;
        }
        
        if (!_sender->start()) {
            printf("ERROR: Server::start() -- start sender failed.\n");
            return false;
        }
        
        return true;
    }

    void IO::addConnection(std::shared_ptr<Connection>& connection) {
        _receiver->addConnection(connection);
        _sender->addConnection(connection);
    }
}
