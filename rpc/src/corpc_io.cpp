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
                    printf("ERROR: IO::Receiver::connectDispatchRoutine read from pipe fd %d ret %d errno %d (%s)\n",
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
                printf("WARN: IO::Receiver::connectDispatchRoutine no task in queue\n");
                
                hopeNum = 1;
            }
        }
    }
    
    void *IO::Receiver::connectionRoutine( void * arg ) {
        co_enable_hook_sys();
        ReceiverTask *recvTask = (ReceiverTask *)arg;
        //Receiver *self = connectedContext->receiver;
        std::shared_ptr<Connection> connection = recvTask->connection;
        delete recvTask;
        
        IO *io = connection->getIO();
        
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
                printf("ERROR: Receiver::connectionRoutine -- read reqhead fd %d ret %d errno %d (%s)\n",
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
            struct pollfd pf = { 0 };
            pf.fd = -1;
            poll( &pf,1,100);
        }
        
        close(fd);
        printf("INFO: end connectHandleRoutine for fd:%d in thread:%d\n", fd, GetPid());
        
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
    
    void IO::MultiThreadReceiver::addConnection(std::shared_ptr<Connection> connection) {
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
    
    void IO::CoroutineReceiver::addConnection(std::shared_ptr<Connection> connection) {
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
            
            if (hopeNum == 0) {
                printf("WARN: IO::Sender::taskQueueRoutine no task in queue\n");
                
                hopeNum = 1;
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
    
    bool IO::MultiThreadSender::start() {
        // 启动线程
        for (std::vector<ThreadData>::iterator it = _threadDatas.begin(); it != _threadDatas.end(); it++) {
            it->_queueContext._sender = this;
            it->_t = std::thread(threadEntry, &(*it));
        }
        
        return true;
    }
    
    void IO::MultiThreadSender::addConnection(std::shared_ptr<Connection> connection) {
        _lastThreadIndex = (_lastThreadIndex + 1) % _threadNum;
        
        connection->setSendThreadIndex(_lastThreadIndex);
        
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::INIT;
        senderTask->connection = connection;
        
        _threadDatas[_lastThreadIndex]._queueContext._queue.push(senderTask);
    }
    
    void IO::MultiThreadSender::removeConnection(std::shared_ptr<Connection> connection) {
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::CLOSE;
        senderTask->connection = connection;
        
        _threadDatas[connection->getSendThreadIndex()]._queueContext._queue.push(senderTask);
    }
    
    void IO::MultiThreadSender::send(std::shared_ptr<Connection> connection, std::shared_ptr<void> data) {
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
    
    void IO::CoroutineSender::addConnection(std::shared_ptr<Connection> connection) {
        connection->setSendThreadIndex(0);
        
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::INIT;
        senderTask->connection = connection;
        
        _queueContext._queue.push(senderTask);
    }
    
    void IO::CoroutineSender::removeConnection(std::shared_ptr<Connection> connection) {
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::CLOSE;
        senderTask->connection = connection;
        
        _queueContext._queue.push(senderTask);
    }
    
    void IO::CoroutineSender::send(std::shared_ptr<Connection> connection, std::shared_ptr<void> data) {
        SenderTask *senderTask = new SenderTask;
        senderTask->type = SenderTask::DATA;
        senderTask->connection = connection;
        senderTask->data = data;
        
        _queueContext._queue.push(senderTask);
    }
    
    IO::IO(uint16_t receiveThreadNum, uint16_t sendThreadNum): _receiveThreadNum(receiveThreadNum), _sendThreadNum(sendThreadNum) {
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

}
