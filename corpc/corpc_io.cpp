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

// TODO: 使用统一的Log接口记录Log

namespace CoRpc {
    
    Decoder::~Decoder() {}
    
    Router::~Router() {}
    
    Encoder::~Encoder() {}
    
    Pipeline::Pipeline(std::shared_ptr<Connection> &connection, uint headSize, uint maxBodySize): _connection(connection), _headSize(headSize), _maxBodySize(maxBodySize), _bodySize(0), _head(headSize,0), _body(maxBodySize,0) {
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
            for (std::vector<std::shared_ptr<Encoder>>::iterator it = _encoders.begin(); it != _encoders.end(); it++) {
                int tmp = 0;
                if ((*it)->encode(connection, connection->_datas.front(), buf + size, space - size, tmp)) {
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
    
    TcpPipeline::TcpPipeline(std::shared_ptr<Connection> &connection, uint headSize, uint maxBodySize, uint bodySizeOffset, SIZE_TYPE bodySizeType): CoRpc::Pipeline(connection, headSize, maxBodySize), _bodySizeOffset(bodySizeOffset), _bodySizeType(bodySizeType), _headNum(0), _bodyNum(0) {
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
            
            void *msg = _decoder->decode(connection, _headBuf, _bodyBuf, _bodySize);
            
            if (!msg) {
                return false;
            }
            
            _router->route(connection, msg);
            
            // 处理完一个请求消息，复位状态
            _headNum = 0;
            _bodyNum = 0;
            _bodySize = 0;
        }
        
        return true;
    }
    
    UdpPipeline::UdpPipeline(std::shared_ptr<Connection> &connection, uint headSize, uint maxBodySize): CoRpc::Pipeline(connection, headSize, maxBodySize) {
        
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
        
        void *msg = _decoder->decode(connection, _headBuf, _bodyBuf, _bodySize);
        
        if (!msg) {
            return false;
        }
        
        _router->route(connection, msg);
        
        return true;
    }
    
    PipelineFactory::~PipelineFactory() {}
    
    Connection::Connection(int fd, IO* io): _fd(fd), _io(io), _routineHang(false), _routine(NULL), _sendThreadIndex(-1), _recvThreadIndex(-1), _isClosing(false), _canClose(false) {
    }
    
    Connection::~Connection() {
        
    }
    
    void Connection::send(std::shared_ptr<void> data) {
        std::shared_ptr<Connection> self = shared_from_this();
        _io->_sender->send(self, data);
    }
    
    Receiver::~Receiver() {
        
    }
    
    void *Receiver::connectionDispatchRoutine( void * arg ) {
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
            ret = (int)read(readFd, &buf[0], 1024);
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
                printf("ERROR: IO::Receiver::connectionRoutine -- read reqhead fd %d ret %d errno %d (%s)\n",
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
        printf("INFO: IO::Receiver::connectionRoutine -- end for fd:%d in thread:%d\n", fd, GetPid());
        
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
            ret = (int)read(readFd, &buf[0], 1024);
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
