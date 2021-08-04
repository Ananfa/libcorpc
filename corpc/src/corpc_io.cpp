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
using namespace corpc;

Worker::Worker::~Worker() {
    
}

// pipe通知版本
void *Worker::msgHandleRoutine( void * arg ) {
    QueueContext *context = (QueueContext*)arg;
    
    WorkerMessageQueue& queue = context->_queue;
    Worker *self = context->_worker;
    
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
                ERROR_LOG("Worker::msgHandleRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                       readFd, ret, errno, strerror(errno));
                
                // TODO: 如何处理？退出协程？
                // sleep 10 milisecond
                msleep(10);
            }
        }
        
        struct timeval t1,t2;
        gettimeofday(&t1, NULL);
        int count = 0;
        
        // 处理任务队列
        void *msg = queue.pop();
        while (msg) {
            self->handleMessage(msg);
            
            // 防止其他协程（如：RoutineEnvironment::cleanRoutine）长时间不被调度，让其他协程处理一下
            count++;
            if (count == 100) {
                gettimeofday(&t2, NULL);
                if ((t2.tv_sec - t1.tv_sec) * 1000000 + t2.tv_usec - t1.tv_usec > 100000) {
                    msleep(1);
                    gettimeofday(&t1, NULL);
                }
                count = 0;
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
    for (auto& td : _threadDatas) {
        td._queueContext._worker = this;
        td._t = std::thread(threadEntry, &td);
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

Pipeline::Pipeline(std::shared_ptr<Connection> &connection, Worker *worker): _connection(connection), _worker(worker) {
}

Pipeline::~Pipeline() {}

MessagePipeline::MessagePipeline(std::shared_ptr<Connection> &connection, Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize): corpc::Pipeline(connection, worker), _decodeFun(decodeFun), _encodeFun(encodeFun), _headSize(headSize), _maxBodySize(maxBodySize), _bodySize(0), _head(headSize,0), _body(maxBodySize,0), _downflowBufSentNum(0) {
    _headBuf = (uint8_t *)_head.data();
    _bodyBuf = (uint8_t *)_body.data();
}

MessagePipeline::~MessagePipeline() {}

bool MessagePipeline::downflow(uint8_t *buf, int space, int &size) {
    std::shared_ptr<Connection> connection = _connection.lock();
    assert(connection);
    
    size = 0;
    
    size_t dlen = _downflowBuf.length();
    if (dlen > 0) {
        assert(dlen > _downflowBufSentNum);
        uint8_t *_dbuf = (uint8_t *)_downflowBuf.data();
        
        if (space >= dlen - _downflowBufSentNum) {
            memcpy(buf, _dbuf + _downflowBufSentNum, dlen - _downflowBufSentNum);
            
            size = int(dlen - _downflowBufSentNum);
            
            _downflowBuf.clear();
            _downflowBufSentNum = 0;
        } else {
            memcpy(buf, _dbuf + _downflowBufSentNum, space);
            
            size = space;
            _downflowBufSentNum += space;
        }
        
        if (space == size) {
            return true;
        }
    }
    
    while (connection->getDataSize() > 0) {
        int tmp = 0;
        if (!_encodeFun(connection, connection->getFrontData(), buf + size, space - size, tmp, _downflowBuf, _downflowBufSentNum)) {
            // 编码失败
            return false;
        }
        
        if (!tmp) { // buf已满，数据放不进buf中（等buf发送出去之后，再放入）
            return true;
        }
        
        size += tmp;
        
        connection->popFrontData();
    }
    
    return true;
}

TcpPipeline::TcpPipeline(std::shared_ptr<Connection> &connection, Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize, uint bodySizeOffset, SIZE_TYPE bodySizeType): corpc::MessagePipeline(connection, worker, decodeFun, encodeFun, headSize, maxBodySize), _bodySizeOffset(bodySizeOffset), _bodySizeType(bodySizeType), _headNum(0), _bodyNum(0) {
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
            if (size - offset >= needNum) {
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
                _bodySize = be16toh(x);
            } else {
                assert(_bodySizeType == FOUR_BYTES);
                uint32_t x = *(uint32_t*)(_headBuf + _bodySizeOffset);
                _bodySize = be32toh(x);
            }
            
            if (_bodySize > _maxBodySize) { // 数据超长
                ERROR_LOG("TcpPipeline::upflow -- request too large in thread\n");
                
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
        
        if (connection->isDecodeError()) {
            return false;
        }
        
        if (msg) {
            _worker->addMessage(msg);
        }
        
        // 处理完一个请求消息，复位状态
        _headNum = 0;
        _bodyNum = 0;
        _bodySize = 0;
    }
    
    return true;
}

UdpPipeline::UdpPipeline(std::shared_ptr<Connection> &connection, Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize): corpc::MessagePipeline(connection, worker, decodeFun, encodeFun, headSize, maxBodySize) {
    
}

bool UdpPipeline::upflow(uint8_t *buf, int size) {
    std::shared_ptr<Connection> connection = _connection.lock();
    assert(connection);
    
    if (size < _headSize) {
        ERROR_LOG("UdpPipeline::upflow -- package size too small\n");
        
        return false;
    }
    
    memcpy(_headBuf, buf, _headSize);
    
    _bodySize = size - _headSize;
    
    if (_bodySize) {
        memcpy(_bodyBuf, buf + _headSize, _bodySize);
    }
    
    void *msg = _decodeFun(connection, _headBuf, _bodyBuf, _bodySize);
    
    if (connection->isDecodeError()) {
        return false;
    }
    
    if (msg) {
        _worker->addMessage(msg);
    }
    
    return true;
}

PipelineFactory::~PipelineFactory() {}

MessagePipelineFactory::~MessagePipelineFactory() {}

std::shared_ptr<corpc::Pipeline> TcpPipelineFactory::buildPipeline(std::shared_ptr<corpc::Connection> &connection) {
    return std::shared_ptr<corpc::Pipeline>( new corpc::TcpPipeline(connection, _worker, _decodeFun, _encodeFun, _headSize, _maxBodySize, _bodySizeOffset, _bodySizeType) );
}

std::shared_ptr<corpc::Pipeline> UdpPipelineFactory::buildPipeline(std::shared_ptr<corpc::Connection> &connection) {
    return std::shared_ptr<corpc::Pipeline>( new corpc::UdpPipeline(connection, _worker, _decodeFun, _encodeFun, _headSize, _maxBodySize) );
}

Connection::Connection(int fd, IO* io, bool needHB): _fd(fd), _io(io), _needHB(needHB), _routineHang(false), _routine(NULL), _sendThreadIndex(-1), _recvThreadIndex(-1), _decodeError(false), _closed(false), _isClosing(false), _canClose(false), _lastRecvHBTime(0) {
}

Connection::~Connection() {
    
}

void Connection::send(std::shared_ptr<void> data) {
    std::shared_ptr<Connection> self = shared_from_this();
    _io->_sender->send(self, data);
}

void Connection::close() {
    if (isOpen()) {
        std::shared_ptr<Connection> self = shared_from_this();
        _io->removeConnection(self);
    }
}

Server::~Server() {}

std::shared_ptr<Connection> Server::buildAndAddConnection(int fd) {
    LOG("fd %d connected\n", fd);
    std::shared_ptr<corpc::Connection> connection(buildConnection(fd));
    std::shared_ptr<corpc::Pipeline> pipeline = _pipelineFactory->buildPipeline(connection);
    connection->setPipeline(pipeline);
    
    // 将接受的连接分别发给Receiver和Sender
    _io->addConnection(connection);
    
    // 判断是否需要心跳
    if (connection->needHB()) {
        Heartbeater::Instance().addConnection(connection);
    }
    
    // 通知连接建立
    onConnect(connection);
    
    return connection;
}

bool Server::start() {
    if (!_acceptor) {
        ERROR_LOG("Server::start() -- acceptor is NULL.\n");
        return false;
    }
    
    if (!_worker) {
        ERROR_LOG("Server::start() -- worker is NULL.\n");
        return false;
    }
    
    // 启动acceptor
    if (!_acceptor->start()) {
        ERROR_LOG("Server::start() -- start acceptor failed.\n");
        return false;
    }
    
    _worker->start();
    
    return true;
}

Acceptor::Acceptor(Server *server, const std::string& ip, uint16_t port): _server(server), _ip(ip), _port(port), _listen_fd(-1) {
    socklen_t addrlen = sizeof(_local_addr);
    bzero(&_local_addr, addrlen);
    _local_addr.sin_family = AF_INET;
    _local_addr.sin_port = htons(_port);
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
    _local_addr.sin_addr.s_addr = nIP;
}

Acceptor::~Acceptor() {
    
}

void *TcpAcceptor::acceptRoutine( void * arg ) {
    TcpAcceptor *self = (TcpAcceptor *)arg;
    Server *server = self->_server;
    int listen_fd = self->_listen_fd;
    
    LOG("start listen %d %s:%d\n", listen_fd, self->_ip.c_str(), self->_port);
    listen( listen_fd, 1024 );
    
    // 注意：由于accept方法没有进行hook，只好将它设置为NONBLOCK并且自己对它进行poll
    int iFlags = fcntl(listen_fd, F_GETFL, 0);
    iFlags |= O_NONBLOCK;
    iFlags |= O_NDELAY;
    fcntl(listen_fd, F_SETFL, iFlags);
    
    // 侦听连接，并把接受的连接传给连接处理对象
    while (true) {
        struct sockaddr_in addr; //maybe sockaddr_un;
        memset( &addr,0,sizeof(addr) );
        socklen_t len = sizeof(addr);
        
        int fd = co_accept(listen_fd, (struct sockaddr *)&addr, &len);
        if( fd < 0 )
        {
            struct pollfd pf = { 0 };
            pf.fd = listen_fd;
            pf.events = (POLLIN|POLLERR|POLLHUP);
            co_poll( co_get_epoll_ct(),&pf,1,10000 );
            continue;
        }
        
        LOG("accept fd %d\n", fd);
        // 保持连接
        setKeepAlive(fd, 10);
        
        // 设置读写超时时间，默认为1秒
        co_set_timeout(fd, -1, 1000);
        
        server->buildAndAddConnection(fd);
    }
    
    return NULL;
}

bool TcpAcceptor::start() {
    if (_port == 0) {
        ERROR_LOG("TcpAcceptor::start() -- port can't be 0\n");
        return false;
    }
    
    _listen_fd = socket(AF_INET,SOCK_STREAM, IPPROTO_TCP);
    if( _listen_fd >= 0 )
    {
        int nReuseAddr = 1;
        setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &nReuseAddr, sizeof(nReuseAddr));
        
        if( bind(_listen_fd, (struct sockaddr*)&_local_addr, sizeof(_local_addr)) == -1 )
        {
            close(_listen_fd);
            _listen_fd = -1;
        }
    }
    
    if(_listen_fd==-1){
        ERROR_LOG("TcpAcceptor::start() -- Can't create socket on %s:%d\n", _ip.c_str(), _port);
        return false;
    }
    
    // 启动accept协程
    RoutineEnvironment::startCoroutine(acceptRoutine, this);
    
    return true;
}

UdpAcceptor::UdpAcceptor(Server *server, const std::string& ip, uint16_t port): Acceptor(server, ip, port), _shakemsg2(CORPC_MESSAGE_HEAD_SIZE, 0), _shakemsg4(CORPC_MESSAGE_HEAD_SIZE, 0), _unshakemsg(CORPC_MESSAGE_HEAD_SIZE, 0) {
    _shakemsg2buf = (uint8_t *)_shakemsg2.data();
    memset(_shakemsg2buf, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int16_t *)(_shakemsg2buf + 4) = htobe16(CORPC_MSG_TYPE_UDP_HANDSHAKE_2);
    _shakemsg4buf = (uint8_t *)_shakemsg4.data();
    memset(_shakemsg4buf, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int16_t *)(_shakemsg4buf + 4) = htobe16(CORPC_MSG_TYPE_UDP_HANDSHAKE_4);
    _unshakemsg2buf = (uint8_t *)_unshakemsg.data();
    memset(_unshakemsg2buf, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int16_t *)(_unshakemsg2buf + 4) = htobe16(CORPC_MSG_TYPE_UDP_UNSHAKE);
}

void UdpAcceptor::threadEntry( UdpAcceptor *self ) {
    // 启动accept协程
    RoutineEnvironment::startCoroutine(acceptRoutine, self);
    RoutineEnvironment::runEventLoop();
}

void *UdpAcceptor::acceptRoutine( void * arg ) {
    UdpAcceptor *self = (UdpAcceptor *)arg;
    int listen_fd = self->_listen_fd;
    char buf[CORPC_MAX_UDP_MESSAGE_SIZE];
    
    co_register_fd(listen_fd);
    co_set_timeout(listen_fd, -1, 1000);
            
    struct sockaddr_in client_addr;
    socklen_t slen = sizeof(client_addr);
    
    LOG("start listen %d %s:%d\n", listen_fd, self->_ip.c_str(), self->_port);
    
    while (true) {
        bzero(&client_addr, slen);
        
        ssize_t ret = recvfrom(listen_fd, buf, CORPC_MAX_UDP_MESSAGE_SIZE, 0, (struct sockaddr *)&client_addr, &slen);
        if (ret != CORPC_MESSAGE_HEAD_SIZE) {
            ERROR_LOG("UdpAcceptor::acceptRoutine() -- wrong msg.\n");
            
            sendto(listen_fd, self->_unshakemsg2buf, CORPC_MESSAGE_HEAD_SIZE, 0, (struct sockaddr *)&client_addr, slen);
            
            continue;
        }
        
        uint32_t bodySize = *(uint32_t *)buf;
        bodySize = be32toh(bodySize);
        int16_t msgType = *(int16_t *)(buf + 4);
        msgType = be16toh(msgType);
        
        // 判断是否“连接请求”消息
        if (msgType != CORPC_MSG_TYPE_UDP_HANDSHAKE_1) {
            if (msgType == CORPC_MSG_TYPE_UDP_HANDSHAKE_3) {
                // 要求客户端重发握手3消息
                DEBUG_LOG("UdpAcceptor::acceptRoutine() -- recv handshake3.\n");
            
                sendto(listen_fd, self->_shakemsg2buf, CORPC_MESSAGE_HEAD_SIZE, 0, (struct sockaddr *)&client_addr, slen);
            } else {
                ERROR_LOG("UdpAcceptor::acceptRoutine() -- not handshake 1 msg.\n");
            }
            
            continue;
        }
        DEBUG_LOG("UdpAcceptor::acceptRoutine() -- recv handshake 1 msg.\n");
        
        int new_fd = self->_shake_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        
        int nReuseAddr = 1;
        setsockopt(new_fd, SOL_SOCKET, SO_REUSEADDR, &nReuseAddr, sizeof(nReuseAddr));
        setsockopt(new_fd, SOL_SOCKET, SO_REUSEPORT, &nReuseAddr, sizeof(nReuseAddr));
        
        if (bind(new_fd , (struct sockaddr *)&self->_local_addr, sizeof(struct sockaddr)) == -1 ||
            connect(new_fd , (struct sockaddr * )&client_addr, sizeof(struct sockaddr)) == -1) {
            ERROR_LOG("UdpAcceptor::acceptRoutine() -- bind and connect new fd\n");
            close(new_fd);
            continue;
        }
        
        // 为new_fd启动握手协程
        RoutineEnvironment::startCoroutine(handshakeRoutine, self);
    }
    
    return NULL;
}

void *UdpAcceptor::handshakeRoutine( void * arg ) {
    UdpAcceptor *self = (UdpAcceptor *)arg;
    Server *server = self->_server;
    int shake_fd = self->_shake_fd; // 注意：这里必须立即记录shake_fd，UdpAcceptor::_shake_fd会被后续新连接修改
    
    std::string bufstring(CORPC_MAX_UDP_MESSAGE_SIZE, 0);
    uint8_t *buf = (uint8_t *)bufstring.data();
    
    // TODO: 设置shake_fd
    
    int waittime = 1000;
    bool shakeOK = false;
    int trytimes = 4;
    
    co_set_timeout(shake_fd, waittime, 1000);
    
    while (trytimes > 0 && !shakeOK) {
        // 发送“连接确认”给客户的
        DEBUG_LOG("Send shake 2 msg\n");
        int ret = (int)write(shake_fd, self->_shakemsg2buf, CORPC_MESSAGE_HEAD_SIZE);
        if (ret != CORPC_MESSAGE_HEAD_SIZE) {
            ERROR_LOG("UdpAcceptor::handshakeRoutine() -- write shake msg 2 fail for fd %d ret %d errno %d (%s)\n",
                   shake_fd, ret, errno, strerror(errno));
            close(shake_fd);
            break;
        }
        
        // 接收“最终确认”
        ret = (int)read(shake_fd, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
        if (ret != CORPC_MESSAGE_HEAD_SIZE) {
            // ret 0 mean disconnected
            if (ret < 0 && errno == EAGAIN) {
                waittime <<= 1;
                trytimes--;
                co_set_timeout(shake_fd, waittime, 1000);
                
                continue;
            }
            
            // 读取的数据长度不对
            ERROR_LOG("UdpAcceptor::handshakeRoutine() -- read shake msg fail for fd %d ret %d errno %d (%s)\n",
                   shake_fd, ret, errno, strerror(errno));
            close(shake_fd);
            break;
        } else {
            // 判断是否“最终确认消息”
            int16_t msgtype = *(int16_t *)(buf + 4);
            msgtype = be16toh(msgtype);
            
            if (msgtype != CORPC_MSG_TYPE_UDP_HANDSHAKE_3) {
                // 消息类型不对
                WARN_LOG("UdpAcceptor::handshakeRoutine() -- not shake 3 msg, fd %d, msgtype %d\n", shake_fd, msgtype);
                continue;
            }
            DEBUG_LOG("recv shake 3 msg, fd:%d\n", shake_fd);

            // 注意：由于发现绑定四元组socket后开始时handshake3消息有较大概率发到listen_fd，因此需要让客户端确认四元组socket确实收到消息
            // 这里需要发个shake4消息给客户端，客户端收到之后才能开始发数据消息（由于客户端能收到shake2消息因此shake4消息也能收到，若收不到就通过心跳机制关闭连接）
            int ret = (int)write(shake_fd, self->_shakemsg4buf, CORPC_MESSAGE_HEAD_SIZE);
            if (ret != CORPC_MESSAGE_HEAD_SIZE) {
                ERROR_LOG("UdpAcceptor::handshakeRoutine() -- write shake msg 4 fail for fd %d ret %d errno %d (%s)\n",
                       shake_fd, ret, errno, strerror(errno));
                close(shake_fd);
                break;
            }

            // 握手成功，创建connection对象
            co_set_timeout(shake_fd, -1, 1000);
            
            server->buildAndAddConnection(shake_fd);
            
            shakeOK = true;
        }
    }
    
    return NULL;
}

bool UdpAcceptor::start() {
    if (_port == 0) {
        ERROR_LOG("UdpAcceptor::start() -- port can't be 0\n");
        return false;
    }
    
    _listen_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if( _listen_fd >= 0 )
    {
        int nReuseAddr = 1;
        setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &nReuseAddr, sizeof(nReuseAddr));
        setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEPORT, &nReuseAddr, sizeof(nReuseAddr));
        
        //bind socket to port
        if( bind(_listen_fd , (struct sockaddr*)&_local_addr, sizeof(_local_addr) ) == -1 )
        {
            close(_listen_fd);
            _listen_fd = -1;
        }
    }
    
    if(_listen_fd==-1){
        ERROR_LOG("UdpAcceptor::start() -- Can't create socket on %s:%d\n", _ip.c_str(), _port);
        return false;
    }
    
    _t = std::thread(threadEntry, this);
    
    return true;
}

Receiver::~Receiver() {}

void *Receiver::connectionDispatchRoutine( void * arg ) {
    QueueContext *context = (QueueContext*)arg;
    
    ReceiverTaskQueue& queue = context->_queue;
    
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
                ERROR_LOG("Receiver::connectDispatchRoutine read from pipe fd %d ret %d errno %d (%s)\n",
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
    ReceiverTask *recvTask = (ReceiverTask *)arg;
    std::shared_ptr<Connection> connection = recvTask->connection;
    delete recvTask;
    
    IO *io = connection->_io;
    
    int fd = connection->getfd();
    LOG("start Receiver::connectionRoutine for fd:%d in thread:%d\n", fd, GetPid());
    
    std::string buffs(CORPC_MAX_BUFFER_SIZE,0);
    uint8_t *buf = (uint8_t *)buffs.data();
    int retryTimes = 0;
    while (true) {
        // 先将数据读到缓存中（尽可能多的读）
        int ret = (int)read(fd, buf, CORPC_MAX_BUFFER_SIZE);
        
        if (ret <= 0) {
            // ret 0 mean disconnected
            if (ret < 0 && errno == EAGAIN) {
                // 这里设置最大重试次数
                if (retryTimes < 5) {
                    msleep(100);
                    retryTimes++;
                    continue;
                }
            }
            
            // 出错处理
            WARN_LOG("Receiver::connectionRoutine -- read reqhead fd %d ret %d errno %d (%s)\n",
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
    
    connection->_closed = true;
    
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
    for (auto& td : _threadDatas) {
        td._queueContext._receiver = this;
        td._t = std::thread(threadEntry, &td);
    }
    
    return true;
}

void MultiThreadReceiver::addConnection(std::shared_ptr<Connection>& connection) {
    uint16_t index = (_lastThreadIndex++) % _threadNum;
    
    connection->setRecvThreadIndex(index);
    
    ReceiverTask *recvTask = new ReceiverTask;
    recvTask->connection = connection;
    
    _threadDatas[index]._queueContext._queue.push(recvTask);
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
                ERROR_LOG("Sender::taskQueueRoutine read from pipe fd %d ret %d errno %d (%s)\n",
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
    
    LOG("start Sender::connectionRoutine for fd:%d in thread:%d\n", connection->getfd(), GetPid());
    
    connection->_routine = co_self();
    connection->_routineHang = false;
    
    std::string buffs(CORPC_MAX_BUFFER_SIZE, 0);
    uint8_t *buf = (uint8_t *)buffs.data();
    uint32_t startIndex = 0;
    uint32_t endIndex = 0;
    
    // 若无数据可以发送则挂起，否则整理发送数据并发送
    while (true) {
        int tmp = 0;
        if (!connection->getPipeline()->downflow(buf + endIndex, CORPC_MAX_BUFFER_SIZE - endIndex, tmp)) {
            break;
        }
        
        endIndex += tmp;
        
        int dataSize = endIndex - startIndex;
        if (dataSize == 0) {
            assert(startIndex == 0);
            // 等数据发完再关
            if (connection->_isClosing) {
                break;
            }
            
            // 挂起
            connection->_routineHang = true;
            co_yield_ct();
            connection->_routineHang = false;
            
            continue;
        }
        
        // 发数据
        int ret;
        do {
            ret = (int)write(connection->_fd, buf + startIndex, dataSize);
            if (ret > 0) {
                assert(ret <= dataSize);
                startIndex += ret;
                dataSize -= ret;
            }
        } while (dataSize > 0 && errno == EAGAIN);
        
        if (dataSize > 0) {
            WARN_LOG("Sender::connectionRoutine -- write resphead fd %d ret %d errno %d (%s)\n",
                   connection->_fd, ret, errno, strerror(errno));
            
            break;
        }
        
        if (startIndex == endIndex) {
            startIndex = endIndex = 0;
        }
    }
    
    connection->_isClosing = true;
    shutdown(connection->_fd, SHUT_RD);
    connection->_canClose = true;
    
    WARN_LOG("Sender::connectionRoutine -- routine end for fd %d\n", connection->_fd);
    
    return NULL;
}

bool MultiThreadSender::start() {
    // 启动线程
    for (auto& td : _threadDatas) {
        td._queueContext._sender = this;
        td._t = std::thread(threadEntry, &td);
    }
    
    return true;
}

void MultiThreadSender::addConnection(std::shared_ptr<Connection>& connection) {
    uint16_t index = (_lastThreadIndex++) % _threadNum;
    
    connection->setSendThreadIndex(index);
    
    SenderTask *senderTask = new SenderTask;
    senderTask->type = SenderTask::INIT;
    senderTask->connection = connection;
    
    _threadDatas[index]._queueContext._queue.push(senderTask);
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

Heartbeater::Heartbeater(): _heartbeatmsg(new SendMessageInfo) {
    _heartbeatmsg->type = CORPC_MSG_TYPE_HEARTBEAT;
    _heartbeatmsg->isRaw = true;
    _heartbeatmsg->needCrypt = false;
    _heartbeatmsg->tag = 0;
    _heartbeatmsg->serial = 0;
    
    _t = std::thread(threadEntry, this);
}

void Heartbeater::threadEntry( Heartbeater *self ) {
    RoutineEnvironment::startCoroutine(heartbeatRoutine, self);
    RoutineEnvironment::startCoroutine(dispatchRoutine, self);
    
    RoutineEnvironment::runEventLoop();
}

void *Heartbeater::dispatchRoutine( void * arg ) {
    Heartbeater *self = (Heartbeater *)arg;
    
    HeartbeatQueue& queue = self->_queue;
    
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
                ERROR_LOG("Heartbeater::dispatchRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                       readFd, ret, errno, strerror(errno));
                
                // TODO: 如何处理？退出协程？
                // sleep 10 milisecond
                msleep(10);
            }
        }
        
        // 处理任务队列
        std::shared_ptr<Connection> connection = queue.pop();
        while (connection) {
            uint64_t nowms = mtime();
            self->_heartbeatList.push_back({connection, nowms + CORPC_HEARTBEAT_PERIOD});
            
            if (self->_heartbeatRoutineHang) {
                co_resume(self->_heartbeatRoutine);
            }
            
            connection = queue.pop();
        }
    }
}

void *Heartbeater::heartbeatRoutine( void * arg ) {
    Heartbeater *self = (Heartbeater *)arg;
    
    self->_heartbeatRoutine = co_self();
    self->_heartbeatRoutineHang = false;

    while (true) {
        if (self->_heartbeatList.empty()) {
            // 挂起
            self->_heartbeatRoutineHang = true;
            co_yield_ct();
            self->_heartbeatRoutineHang = false;
            
            continue;
        }
        
        uint64_t nowms = mtime();
        
        HeartbeatItem item = self->_heartbeatList.front();
        self->_heartbeatList.pop_front();
        
        if (item.connection->_closed) {
            continue;
        }
        
        if (!item.connection->getLastRecvHBTime()) {
            item.connection->setLastRecvHBTime(nowms);
        }
        
        if (nowms - item.connection->getLastRecvHBTime() > CORPC_MAX_NO_HEARTBEAT_TIME) {
            // 心跳超时，断线处理
            ERROR_LOG("Heartbeater::heartbeatRoutine() -- heartbeat timeout for fd %d\n", item.connection->getfd());
            item.connection->close();
            continue;
        }
        
        // 注意: 这里有个问题，当连接已经closed时，需要等到心跳时间到达才会被处理，而心跳时长是5秒，因此已断线的连接对象会最长保持5秒
        if (item.nexttime > nowms) {
            msleep(item.nexttime - nowms);
            
            nowms = mtime();
        }
        
        if (item.connection->_closed) {
            continue;
        }
        
        // 发心跳包
        item.connection->send(self->_heartbeatmsg);
        item.nexttime = nowms + CORPC_HEARTBEAT_PERIOD;
        self->_heartbeatList.push_back(item);
    }
}

void Heartbeater::addConnection(std::shared_ptr<Connection>& connection) {
    _queue.push(connection);
}

IO::IO(uint16_t receiveThreadNum, uint16_t sendThreadNum): _receiveThreadNum(receiveThreadNum), _sendThreadNum(sendThreadNum) {
}

IO* IO::create(uint16_t receiveThreadNum, uint16_t sendThreadNum) {
    if (receiveThreadNum == 0 && sendThreadNum == 0) {
        ERROR_LOG("IO::create() -- sender and receiver can't run at same thread.\n");
        return nullptr;
    }
    
    IO *io = new IO(receiveThreadNum, sendThreadNum);
    io->start();
    
    return io;
}

bool IO::start() {
    if (_sendThreadNum == 0 && _receiveThreadNum == 0) {
        ERROR_LOG("IO::start() -- sender and receiver can't run at same thread.\n");
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
        ERROR_LOG("IO::start() -- start receiver failed.\n");
        return false;
    }
    
    if (!_sender->start()) {
        ERROR_LOG("IO::start() -- start sender failed.\n");
        return false;
    }
    
    return true;
}

void IO::addConnection(std::shared_ptr<Connection>& connection) {
    // 注意：以下两行顺序不能调换，不然会有多线程问题
    _sender->addConnection(connection);
    _receiver->addConnection(connection);
}

void IO::removeConnection(std::shared_ptr<Connection>& connection) {
    _sender->removeConnection(connection);
}
