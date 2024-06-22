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

Worker::~Worker() {
    
}

void Worker::addMessage(void *msg) {
    queue_.push(msg);
}

void *Worker::msgHandleRoutine( void * arg ) {
    Worker *self = (Worker *)arg;
    WorkerMessageQueue& queue = self->queue_;

    while (true) {
        // 处理任务队列
        void *msg = queue.pop();

        self->handleMessage(msg);
        
        RoutineEnvironment::pauseIfRuntimeBusy();
    }
}

MultiThreadWorker::~MultiThreadWorker() {}

void MultiThreadWorker::threadEntry( Worker *self ) {
    // 启动rpc任务处理协程
    RoutineEnvironment::startCoroutine(msgHandleRoutine, self);
    
    RoutineEnvironment::runEventLoop();
}

void MultiThreadWorker::start() {
    // 启动线程
    for (int i = 0; i < threadNum_; i++) {
        ts_[i] = std::thread(threadEntry, this);
    }
}

CoroutineWorker::~CoroutineWorker() {}

void CoroutineWorker::start() {
    // 启动rpc任务处理协程
    RoutineEnvironment::startCoroutine(msgHandleRoutine, this);
}

Pipeline::Pipeline(std::shared_ptr<Connection> &connection, Worker *worker): connection_(connection), worker_(worker) {
}

Pipeline::~Pipeline() {}

MessagePipeline::MessagePipeline(std::shared_ptr<Connection> &connection, Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize): corpc::Pipeline(connection, worker), decodeFun_(decodeFun), encodeFun_(encodeFun), headSize_(headSize), maxBodySize_(maxBodySize), bodySize_(0), head_(headSize,0), body_(maxBodySize,0), downflowBufSentNum_(0) {
    headBuf_ = (uint8_t *)head_.data();
    bodyBuf_ = (uint8_t *)body_.data();
}

MessagePipeline::~MessagePipeline() {}

bool MessagePipeline::downflow(uint8_t *buf, int space, int &size) {
    std::shared_ptr<Connection> connection = connection_.lock();
    assert(connection);
    
    size = 0;
    
    size_t dlen = downflowBuf_.length();
    if (dlen > 0) {
        assert(dlen > downflowBufSentNum_);
        uint8_t *_dbuf = (uint8_t *)downflowBuf_.data();
        
        if (space >= dlen - downflowBufSentNum_) {
            memcpy(buf, _dbuf + downflowBufSentNum_, dlen - downflowBufSentNum_);
            
            size = int(dlen - downflowBufSentNum_);
            
            downflowBuf_.clear();
            downflowBufSentNum_ = 0;
        } else {
            memcpy(buf, _dbuf + downflowBufSentNum_, space);
            
            size = space;
            downflowBufSentNum_ += space;
        }
        
        if (space == size) {
            return true;
        }
    }
    
    while (connection->getDataSize() > 0) {
        int tmp = 0;
        if (!encodeFun_(connection, connection->getFrontData(), buf + size, space - size, tmp, downflowBuf_, downflowBufSentNum_)) {
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

TcpPipeline::TcpPipeline(std::shared_ptr<Connection> &connection, Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize, uint bodySizeOffset, SIZE_TYPE bodySizeType): corpc::MessagePipeline(connection, worker, decodeFun, encodeFun, headSize, maxBodySize), bodySizeOffset_(bodySizeOffset), bodySizeType_(bodySizeType), headNum_(0), bodyNum_(0) {
}

bool TcpPipeline::upflow(uint8_t *buf, int size) {
    std::shared_ptr<Connection> connection = connection_.lock();
    assert(connection);
    
    // 解析数据
    int offset = 0;
    while (size > offset) {
        // 先解析头部
        if (headNum_ < headSize_) {
            int needNum = headSize_ - headNum_;
            if (size - offset >= needNum) {
                memcpy(headBuf_ + headNum_, buf + offset, needNum);
                headNum_ = headSize_;
                
                offset += needNum;
            } else {
                memcpy(headBuf_ + headNum_, buf + offset, size - offset);
                headNum_ += size - offset;
                
                break;
            }
        }
        
        if (!bodySize_) {
            // 解析消息长度值
            if (bodySizeType_ == TWO_BYTES) {
                uint16_t x = *(uint16_t*)(headBuf_ + bodySizeOffset_);
                bodySize_ = be16toh(x);
            } else {
                assert(bodySizeType_ == FOUR_BYTES);
                uint32_t x = *(uint32_t*)(headBuf_ + bodySizeOffset_);
                bodySize_ = be32toh(x);
            }
            
            if (bodySize_ > maxBodySize_) { // 数据超长
                ERROR_LOG("TcpPipeline::upflow -- request too large in thread, %d > %d\n", bodySize_, maxBodySize_);
                
                return false;
            }
        }
        
        // 从缓存中解析数据
        if (bodyNum_ < bodySize_) {
            int needNum = bodySize_ - bodyNum_;
            if (size - offset >= needNum) {
                memcpy(bodyBuf_ + bodyNum_, buf + offset, needNum);
                bodyNum_ = bodySize_;
                
                offset += needNum;
            } else {
                memcpy(bodyBuf_ + bodyNum_, buf + offset, size - offset);
                bodyNum_ += size - offset;
                
                break;
            }
        }
        
        void *msg = decodeFun_(connection, headBuf_, bodyBuf_, bodySize_);
        
        if (connection->isDecodeError()) {
            return false;
        }
        
        if (msg) {
            worker_->addMessage(msg);
        }
        
        // 处理完一个请求消息，复位状态
        headNum_ = 0;
        bodyNum_ = 0;
        bodySize_ = 0;
    }
    
    return true;
}

UdpPipeline::UdpPipeline(std::shared_ptr<Connection> &connection, Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize): corpc::MessagePipeline(connection, worker, decodeFun, encodeFun, headSize, maxBodySize) {
    
}

bool UdpPipeline::upflow(uint8_t *buf, int size) {
    std::shared_ptr<Connection> connection = connection_.lock();
    assert(connection);
    
    if (size < headSize_) {
        ERROR_LOG("UdpPipeline::upflow -- package size too small\n");
        
        return false;
    }
    
    memcpy(headBuf_, buf, headSize_);
    
    bodySize_ = size - headSize_;
    
    if (bodySize_) {
        memcpy(bodyBuf_, buf + headSize_, bodySize_);
    }
    
    void *msg = decodeFun_(connection, headBuf_, bodyBuf_, bodySize_);
    
    if (connection->isDecodeError()) {
        return false;
    }
    
    if (msg) {
        worker_->addMessage(msg);
    }
    
    return true;
}

PipelineFactory::~PipelineFactory() {}

MessagePipelineFactory::~MessagePipelineFactory() {}

std::shared_ptr<corpc::Pipeline> TcpPipelineFactory::buildPipeline(std::shared_ptr<corpc::Connection> &connection) {
    return std::shared_ptr<corpc::Pipeline>( new corpc::TcpPipeline(connection, worker_, decodeFun_, encodeFun_, headSize_, maxBodySize_, bodySizeOffset_, bodySizeType_) );
}

std::shared_ptr<corpc::Pipeline> UdpPipelineFactory::buildPipeline(std::shared_ptr<corpc::Connection> &connection) {
    return std::shared_ptr<corpc::Pipeline>( new corpc::UdpPipeline(connection, worker_, decodeFun_, encodeFun_, headSize_, maxBodySize_) );
}

Connection::Connection(int fd, IO* io, bool needHB): fd_(fd), io_(io), needHB_(needHB), routineHang_(false), routine_(NULL), sendThreadIndex_(-1), recvThreadIndex_(-1), decodeError_(false), closed_(false), isClosing_(false), closeSem_(0), lastRecvHBTime_(0) {
}

Connection::~Connection() {
    
}

void Connection::send(std::shared_ptr<void> data) {
    std::shared_ptr<Connection> self = shared_from_this();
    io_->sender_->send(self, data);
}

void Connection::close() {
    if (isOpen()) {
        std::shared_ptr<Connection> self = shared_from_this();
        io_->removeConnection(self);

        if (self->needHB()) {
            Heartbeater::Instance().removeConnection(self);
        }
    }
}

ssize_t Connection::write(const void *buf, size_t nbyte) {
    int ret;
    uint32_t sentNum = 0;
    uint32_t leftNum = nbyte;
    do {
        ret = (int)::write(fd_, buf + sentNum, leftNum);
        if (ret > 0) {
            assert(ret <= leftNum);
            sentNum += ret;
            leftNum -= ret;
        }
    } while (leftNum > 0 && (errno == EAGAIN || errno == EINTR));

    if (leftNum > 0) {
        WARN_LOG("Connection::write -- write fd %d ret %d errno %d (%s)\n",
                   fd_, ret, errno, strerror(errno));
        return -1;
    }

    return sentNum;
}

Server::~Server() {}

std::shared_ptr<Connection> Server::buildAndAddConnection(int fd) {
    LOG("fd %d connected\n", fd);
    std::shared_ptr<corpc::Connection> connection(buildConnection(fd));
    std::shared_ptr<corpc::Pipeline> pipeline = pipelineFactory_->buildPipeline(connection);
    connection->setPipeline(pipeline);
    
    // 注意：onConnect原先是放在最后处理，现在调整到这里。原因是发现放在最后会出现连接消息处理前就收到业务消息处理，经过
    // 分析，将onConnect调整到这里不会出现“onConnect中会有conn->close()操作导致连接未加到IO就先要从IO删除的问题”
    // 通知连接建立
    onConnect(connection);
    
    // 将接受的连接分别发给Receiver和Sender
    io_->addConnection(connection);
    
    // 判断是否需要心跳
    if (connection->needHB()) {
        Heartbeater::Instance().addConnection(connection);
    }
    
    return connection;
}

bool Server::start() {
    if (!acceptor_) {
        ERROR_LOG("Server::start() -- acceptor is NULL.\n");
        return false;
    }
    
    if (!worker_) {
        ERROR_LOG("Server::start() -- worker is NULL.\n");
        return false;
    }
    
    // 启动acceptor
    if (!acceptor_->start()) {
        ERROR_LOG("Server::start() -- start acceptor failed.\n");
        return false;
    }
    
    worker_->start();
    
    return true;
}

Acceptor::Acceptor(Server *server, const std::string& ip, uint16_t port): server_(server), ip_(ip), port_(port), listen_fd_(-1) {
    socklen_t addrlen = sizeof(local_addr_);
    bzero(&local_addr_, addrlen);
    local_addr_.sin_family = AF_INET;
    local_addr_.sin_port = htons(port_);
    int nIP = 0;
    
    if (ip_.empty() ||
        ip_.compare("0") == 0 ||
        ip_.compare("0.0.0.0") == 0 ||
        ip_.compare("*") == 0) {
        nIP = htonl(INADDR_ANY);
    }
    else
    {
        nIP = inet_addr(ip_.c_str());
    }
    local_addr_.sin_addr.s_addr = nIP;
}

Acceptor::~Acceptor() {
    
}

void *TcpAcceptor::acceptRoutine( void * arg ) {
    TcpAcceptor *self = (TcpAcceptor *)arg;
    Server *server = self->server_;
    int listen_fd = self->listen_fd_;
    
    LOG("start listen %d %s:%d\n", listen_fd, self->ip_.c_str(), self->port_);
    listen( listen_fd, 1024 );
    
    // 注意：由于accept方法没有进行hook，只好将它设置为NONBLOCK并且自己对它进行poll
    int iFlags = fcntl(listen_fd, F_GETFL, 0);
    iFlags |= O_NONBLOCK;
    iFlags |= O_NDELAY;
    fcntl(listen_fd, F_SETFL, iFlags);
    
    // 侦听连接，并把接受的连接传给连接处理对象
    while (true) {
        sockaddr_in addr; //maybe sockaddr_un;
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
    if (port_ == 0) {
        ERROR_LOG("TcpAcceptor::start() -- port can't be 0\n");
        return false;
    }
    
    listen_fd_ = socket(AF_INET,SOCK_STREAM, IPPROTO_TCP);
    if( listen_fd_ >= 0 )
    {
        int nReuseAddr = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &nReuseAddr, sizeof(nReuseAddr));
        
        if( bind(listen_fd_, (struct sockaddr*)&local_addr_, sizeof(local_addr_)) == -1 )
        {
            close(listen_fd_);
            listen_fd_ = -1;
        }
    }
    
    if(listen_fd_==-1){
        ERROR_LOG("TcpAcceptor::start() -- Can't create socket on %s:%d\n", ip_.c_str(), port_);
        return false;
    }
    
    // 启动accept协程
    RoutineEnvironment::startCoroutine(acceptRoutine, this);
    
    return true;
}

UdpAcceptor::UdpAcceptor(Server *server, const std::string& ip, uint16_t port): Acceptor(server, ip, port), shakemsg2_(CORPC_MESSAGE_HEAD_SIZE, 0), shakemsg4_(CORPC_MESSAGE_HEAD_SIZE, 0), unshakemsg_(CORPC_MESSAGE_HEAD_SIZE, 0) {
    shakemsg2buf_ = (uint8_t *)shakemsg2_.data();
    memset(shakemsg2buf_, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int16_t *)(shakemsg2buf_ + 4) = htobe16(CORPC_MSG_TYPE_UDP_HANDSHAKE_2);
    shakemsg4buf_ = (uint8_t *)shakemsg4_.data();
    memset(shakemsg4buf_, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int16_t *)(shakemsg4buf_ + 4) = htobe16(CORPC_MSG_TYPE_UDP_HANDSHAKE_4);
    unshakemsg2buf_ = (uint8_t *)unshakemsg_.data();
    memset(unshakemsg2buf_, 0, CORPC_MESSAGE_HEAD_SIZE);
    *(int16_t *)(unshakemsg2buf_ + 4) = htobe16(CORPC_MSG_TYPE_UDP_UNSHAKE);
}

void UdpAcceptor::threadEntry( UdpAcceptor *self ) {
    // 启动accept协程
    RoutineEnvironment::startCoroutine(acceptRoutine, self);
    RoutineEnvironment::runEventLoop();
}

void *UdpAcceptor::acceptRoutine( void * arg ) {
    UdpAcceptor *self = (UdpAcceptor *)arg;
    int listen_fd = self->listen_fd_;
    
    std::string bufstring(CORPC_MAX_UDP_MESSAGE_SIZE, 0);
    uint8_t *buf = (uint8_t *)bufstring.data();
    
    co_register_fd(listen_fd);
    co_set_timeout(listen_fd, -1, 1000);
            
    sockaddr_in client_addr;
    socklen_t slen = sizeof(client_addr);
    
    LOG("start listen %d %s:%d\n", listen_fd, self->ip_.c_str(), self->port_);
    
    while (true) {
        bzero(&client_addr, slen);
        
        ssize_t ret = recvfrom(listen_fd, buf, CORPC_MAX_UDP_MESSAGE_SIZE, 0, (struct sockaddr *)&client_addr, &slen);
        if (ret != CORPC_MESSAGE_HEAD_SIZE) {
            ERROR_LOG("UdpAcceptor::acceptRoutine() -- wrong msg.\n");
            
            sendto(listen_fd, self->unshakemsg2buf_, CORPC_MESSAGE_HEAD_SIZE, 0, (struct sockaddr *)&client_addr, slen);
            
            continue;
        }
DEBUG_LOG("UdpAcceptor::acceptRoutine() -- recv from listen_fd.\n");
        uint32_t bodySize = *(uint32_t *)buf;
        bodySize = be32toh(bodySize);
        int16_t msgType = *(int16_t *)(buf + 4);
        msgType = be16toh(msgType);
        
        // 判断是否“连接请求”消息
        if (msgType != CORPC_MSG_TYPE_UDP_HANDSHAKE_1) {
            if (msgType == CORPC_MSG_TYPE_UDP_HANDSHAKE_3) {
                // 要求客户端重发握手3消息（应该由绑定四元组的new_fd接收握手3消息，但有时会被listen_fd接收）
                DEBUG_LOG("UdpAcceptor::acceptRoutine() -- recv handshake3.\n");
            
                sendto(listen_fd, self->shakemsg2buf_, CORPC_MESSAGE_HEAD_SIZE, 0, (struct sockaddr *)&client_addr, slen);
            } else {
                ERROR_LOG("UdpAcceptor::acceptRoutine() -- not handshake 1 msg.\n");
            }
            
            continue;
        }
        DEBUG_LOG("UdpAcceptor::acceptRoutine() -- recv handshake 1 msg.\n");

        // 过滤多余的HANDSHAKE_1消息
        auto it = self->shakingClient_.find(client_addr);
        if (it != self->shakingClient_.end()) {
            ERROR_LOG("UdpAcceptor::acceptRoutine() -- duplicate handshake 1 msg.\n");
            continue;
        }
        
        int new_fd = self->shake_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        
        int nReuseAddr = 1;
        setsockopt(new_fd, SOL_SOCKET, SO_REUSEADDR, &nReuseAddr, sizeof(nReuseAddr));
        setsockopt(new_fd, SOL_SOCKET, SO_REUSEPORT, &nReuseAddr, sizeof(nReuseAddr));
        
        if (bind(new_fd , (struct sockaddr *)&self->local_addr_, sizeof(struct sockaddr)) == -1 ||
            connect(new_fd , (struct sockaddr * )&client_addr, sizeof(struct sockaddr)) == -1) {
            ERROR_LOG("UdpAcceptor::acceptRoutine() -- bind and connect new fd\n");
            close(new_fd);
            continue;
        }
        
        self->shakingClient_.insert(std::make_pair(client_addr, true));

        // 为new_fd启动握手协程
        HandshakeInfo *info = new HandshakeInfo();
        info->self = self;
        info->addr = client_addr;
        RoutineEnvironment::startCoroutine(handshakeRoutine, info);
    }
    
    return NULL;
}

void *UdpAcceptor::handshakeRoutine( void * arg ) {
    HandshakeInfo *info = (HandshakeInfo *)arg;
    UdpAcceptor *self = info->self;
    sockaddr_in client_addr = info->addr;
    delete info;

    Server *server = self->server_;
    int shake_fd = self->shake_fd_; // 注意：这里必须立即记录shake_fd，UdpAcceptor::_shake_fd会被后续新连接修改
    
    std::string bufstring(CORPC_MAX_UDP_MESSAGE_SIZE, 0);
    uint8_t *buf = (uint8_t *)bufstring.data();
    
    // TODO: 设置shake_fd
    
    int waittime = 1000;
    bool shakeOK = false;
    int trytimes = 4;
    
    co_set_timeout(shake_fd, waittime, 1000);
    
    while (trytimes > 0 && !shakeOK) {
        // 发送“连接确认”给客户的
        DEBUG_LOG("Send shake 2 msg, fd %d\n", shake_fd);
        int ret = (int)write(shake_fd, self->shakemsg2buf_, CORPC_MESSAGE_HEAD_SIZE);
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
            int ret = (int)write(shake_fd, self->shakemsg4buf_, CORPC_MESSAGE_HEAD_SIZE);
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

    self->shakingClient_.erase(client_addr);
    
    return NULL;
}

bool UdpAcceptor::start() {
    if (port_ == 0) {
        ERROR_LOG("UdpAcceptor::start() -- port can't be 0\n");
        return false;
    }
    
    listen_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if( listen_fd_ >= 0 )
    {
        int nReuseAddr = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &nReuseAddr, sizeof(nReuseAddr));
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &nReuseAddr, sizeof(nReuseAddr));
        
        //bind socket to port
        if( bind(listen_fd_ , (struct sockaddr*)&local_addr_, sizeof(local_addr_) ) == -1 )
        {
            close(listen_fd_);
            listen_fd_ = -1;
        }
    }
    
    if(listen_fd_==-1){
        ERROR_LOG("UdpAcceptor::start() -- Can't create socket on %s:%d\n", ip_.c_str(), port_);
        return false;
    }
    
    t_ = std::thread(threadEntry, this);
    
    return true;
}

Receiver::~Receiver() {}

void *Receiver::connectionDispatchRoutine( void * arg ) {
    QueueContext *context = (QueueContext*)arg;
    
    ReceiverTaskQueue& queue = context->queue_;
    
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
            recvTask->connection->onReceiverInit();
            RoutineEnvironment::startCoroutine(connectionRoutine, recvTask);
            
            recvTask = queue.pop();
        }
    }
}

void *Receiver::connectionRoutine( void * arg ) {
    // TODO: 限流，用滑动窗口算法进行限流，connection中增加限流标记（只对客户端的连接限流），触发限流阈值断线
    ReceiverTask *recvTask = (ReceiverTask *)arg;
    std::shared_ptr<Connection> connection = recvTask->connection;
    delete recvTask;
    
    IO *io = connection->io_;
    
    int fd = connection->getfd();
    DEBUG_LOG("start Receiver::connectionRoutine for fd:%d in thread:%d\n", fd, GetPid());
    
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
            
            // 出错处理（断线）
            DEBUG_LOG("Receiver::connectionRoutine -- read reqhead fd %d ret %d errno %d (%s)\n",
                   fd, ret, errno, strerror(errno));
            
            break;
        }
        
        if (!connection->getPipeline()->upflow(buf, ret)) {
            break;
        }
    }
DEBUG_LOG("Receiver::connectionRoutine -- 1\n");
    io->sender_->removeConnection(connection); // 通知sender关闭connection
    shutdown(fd, SHUT_WR);  // 让sender中的fd相关协程退出
    
DEBUG_LOG("Receiver::connectionRoutine -- 2\n");
    // 等待写关闭
    connection->closeSem_.wait();
    //while (!connection->canClose_) {
    //    // sleep 100 milisecond
    //    msleep(100);
    //}
    
DEBUG_LOG("Receiver::connectionRoutine -- 3\n");
    close(fd);
    
    connection->closed_ = true;
    
DEBUG_LOG("Receiver::connectionRoutine -- 4\n");
    connection->onClose();
    DEBUG_LOG("Receiver::connectionRoutine -- routine end for fd %d\n", fd);
    return NULL;
}

void MultiThreadReceiver::threadEntry(ThreadData *tdata) {
    // 启动处理待处理连接协程
    RoutineEnvironment::startCoroutine(connectionDispatchRoutine, &tdata->queueContext_);
    
    RoutineEnvironment::runEventLoop();
}

bool MultiThreadReceiver::start() {
    // 启动线程
    for (auto& td : threadDatas_) {
        td.queueContext_.receiver_ = this;
        td.t_ = std::thread(threadEntry, &td);
    }
    
    return true;
}

void MultiThreadReceiver::addConnection(std::shared_ptr<Connection>& connection) {
    uint16_t index = (lastThreadIndex_++) % threadNum_;
    
    connection->setRecvThreadIndex(index);
    
    ReceiverTask *recvTask = new ReceiverTask;
    recvTask->connection = connection;
    
    threadDatas_[index].queueContext_.queue_.push(recvTask);
}

bool CoroutineReceiver::start() {
    RoutineEnvironment::startCoroutine(connectionDispatchRoutine, &queueContext_);
    
    return true;
}

void CoroutineReceiver::addConnection(std::shared_ptr<Connection>& connection) {
    connection->setRecvThreadIndex(0);
    
    ReceiverTask *recvTask = new ReceiverTask;
    recvTask->connection = connection;
    
    queueContext_.queue_.push(recvTask);
}

Sender::~Sender() {
    
}

void *Sender::taskQueueRoutine( void * arg ) {
    QueueContext *context = (QueueContext*)arg;
    
    SenderTaskQueue& queue = context->queue_;
    
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
                    task->connection->onSenderInit();
                    RoutineEnvironment::startCoroutine(connectionRoutine, task);
                    break;
                    
                case SenderTask::CLOSE:
                    if (!task->connection->isClosing_) {
                        task->connection->isClosing_ = true;
                        
                        if (task->connection->routineHang_) {
                            co_resume(task->connection->routine_);
                        }
                    }
                    
                    delete task;
                    break;
                    
                case SenderTask::DATA:
                    // 若连接未关闭，放入connection的等待发送队列，若connection协程挂起则唤醒执行
                    if (!task->connection->isClosing_) {
                        task->connection->datas_.push_back(task->data);
                        
                        // 唤醒挂起的connection协程
                        if (task->connection->routineHang_) {
                            co_resume(task->connection->routine_);
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
    
    DEBUG_LOG("start Sender::connectionRoutine for fd:%d in thread:%d\n", connection->getfd(), GetPid());
    
    connection->routine_ = co_self();
    connection->routineHang_ = false;
    
    std::string buffs(CORPC_MAX_BUFFER_SIZE, 0);
    uint8_t *buf = (uint8_t *)buffs.data();
    //uint32_t startIndex = 0;
    //uint32_t endIndex = 0;
    
    // 若无数据可以发送则挂起，否则整理发送数据并发送
    while (true) {
        int dataSize = 0;
        if (!connection->getPipeline()->downflow(buf/* + endIndex*/, CORPC_MAX_BUFFER_SIZE/* - endIndex*/, dataSize)) {
            break;
        }
        
        //endIndex += tmp;
        
        //int dataSize = endIndex - startIndex;
        if (dataSize == 0) {
            //assert(startIndex == 0);
            // 等数据发完再关
            if (connection->isClosing_) {
                break;
            }
            
            // 挂起
            connection->routineHang_ = true;
            co_yield_ct();
            connection->routineHang_ = false;
            
            continue;
        }
        
        // 发数据
        int ret = connection->write(buf/* + startIndex*/, dataSize);
        if (ret < 0) {
            break;
        }

        assert(ret == dataSize);
        //startIndex = endIndex = 0;


//        int ret;
//        do {
//            ret = (int)write(connection->fd_, buf + startIndex, dataSize);
//            if (ret > 0) {
//                assert(ret <= dataSize);
//                startIndex += ret;
//                dataSize -= ret;
//            }
//        } while (dataSize > 0 && errno == EAGAIN);
//        
//        if (dataSize > 0) {
//            WARN_LOG("Sender::connectionRoutine -- write resphead fd %d ret %d errno %d (%s)\n",
//                   connection->fd_, ret, errno, strerror(errno));
//            
//            break;
//        }
//        
//        assert(startIndex == endIndex);
//        if (startIndex == endIndex) {
//            startIndex = endIndex = 0;
//        }
    }
    
    connection->isClosing_ = true;
    shutdown(connection->fd_, SHUT_RD);
    connection->closeSem_.post();
    //connection->canClose_ = true;
    
    DEBUG_LOG("Sender::connectionRoutine -- routine end for fd %d\n", connection->fd_);
    
    return NULL;
}

bool MultiThreadSender::start() {
    // 启动线程
    for (auto& td : threadDatas_) {
        td.queueContext_.sender_ = this;
        td.t_ = std::thread(threadEntry, &td);
    }
    
    return true;
}

void MultiThreadSender::addConnection(std::shared_ptr<Connection>& connection) {
    uint16_t index = (lastThreadIndex_++) % threadNum_;
    
    connection->setSendThreadIndex(index);
    
    SenderTask *senderTask = new SenderTask;
    senderTask->type = SenderTask::INIT;
    senderTask->connection = connection;
    
    threadDatas_[index].queueContext_.queue_.push(senderTask);
}

void MultiThreadSender::removeConnection(std::shared_ptr<Connection>& connection) {
    SenderTask *senderTask = new SenderTask;
    senderTask->type = SenderTask::CLOSE;
    senderTask->connection = connection;
    
    threadDatas_[connection->getSendThreadIndex()].queueContext_.queue_.push(senderTask);
}

void MultiThreadSender::send(std::shared_ptr<Connection>& connection, std::shared_ptr<void> data) {
    SenderTask *senderTask = new SenderTask;
    senderTask->type = SenderTask::DATA;
    senderTask->connection = connection;
    senderTask->data = data;
    
    threadDatas_[connection->getSendThreadIndex()].queueContext_.queue_.push(senderTask);
}

void MultiThreadSender::threadEntry( ThreadData *tdata ) {
    // 启动send协程
    RoutineEnvironment::startCoroutine(taskQueueRoutine, &tdata->queueContext_);
    
    RoutineEnvironment::runEventLoop();
}

bool CoroutineSender::start() {
    RoutineEnvironment::startCoroutine(taskQueueRoutine, &queueContext_);
    
    return true;
}

void CoroutineSender::addConnection(std::shared_ptr<Connection>& connection) {
    connection->setSendThreadIndex(0);
    
    SenderTask *senderTask = new SenderTask;
    senderTask->type = SenderTask::INIT;
    senderTask->connection = connection;
    
    queueContext_.queue_.push(senderTask);
}

void CoroutineSender::removeConnection(std::shared_ptr<Connection>& connection) {
    SenderTask *senderTask = new SenderTask;
    senderTask->type = SenderTask::CLOSE;
    senderTask->connection = connection;
    
    queueContext_.queue_.push(senderTask);
}

void CoroutineSender::send(std::shared_ptr<Connection>& connection, std::shared_ptr<void> data) {
    SenderTask *senderTask = new SenderTask;
    senderTask->type = SenderTask::DATA;
    senderTask->connection = connection;
    senderTask->data = data;
    
    queueContext_.queue_.push(senderTask);
}

Heartbeater::Heartbeater(): heartbeatmsg_(new SendMessageInfo) {
    heartbeatmsg_->type = CORPC_MSG_TYPE_HEARTBEAT;
    heartbeatmsg_->isRaw = true;
    heartbeatmsg_->needCrypt = false;
    heartbeatmsg_->tag = 0;
    heartbeatmsg_->serial = 0;
    
    t_ = std::thread(threadEntry, this);
}

void Heartbeater::threadEntry( Heartbeater *self ) {
    RoutineEnvironment::startCoroutine(heartbeatRoutine, self);
    RoutineEnvironment::startCoroutine(dispatchRoutine, self);
    
    RoutineEnvironment::runEventLoop();
}

void *Heartbeater::dispatchRoutine( void * arg ) {
    Heartbeater *self = (Heartbeater *)arg;
    
    HeartbeatQueue& queue = self->queue_;
    
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
        HeartbeatTask* task = queue.pop();
        while (task) {
            if (task->type == HeartbeatTask::START) {
                uint64_t nowms = mtime();
                self->heartbeatList_.insert((uint64_t)task->connection.get(), nowms + CORPC_HEARTBEAT_PERIOD, task->connection);

                if (self->heartbeatRoutineHang_) {
                    co_resume(self->heartbeatRoutine_);
                }
            } else { // task->type == HeartbeatTask::STOP
                auto node = self->heartbeatList_.getNode((uint64_t)task->connection.get());
                if (node) {
                    DEBUG_LOG("Heartbeater::dispatchRoutine() -- remove conn: %lu fd %d\n", (uint64_t)task->connection.get(), task->connection->getfd());
                    self->heartbeatList_.remove(node);
                }
            }

            delete task;
            task = queue.pop();
        }
    }
}

void *Heartbeater::heartbeatRoutine( void * arg ) {
    Heartbeater *self = (Heartbeater *)arg;
    
    self->heartbeatRoutine_ = co_self();
    self->heartbeatRoutineHang_ = false;

    while (true) {
        if (self->heartbeatList_.empty()) {
            // 挂起
            self->heartbeatRoutineHang_ = true;
            co_yield_ct();
            self->heartbeatRoutineHang_ = false;
            
            continue;
        }
        
        uint64_t nowms = mtime();
        
        auto node = self->heartbeatList_.getLast();
        uint64_t expireTime = node->expireTime;
        std::shared_ptr<Connection> conn = node->data;

        if (conn->closed_) {
            DEBUG_LOG("Heartbeater::heartbeatRoutine() -- remove conn: %lu fd %d\n", (uint64_t)conn.get(), conn->getfd());
            self->heartbeatList_.remove(node);
            continue;
        }

        if (!conn->getLastRecvHBTime()) {
            // 刚加入心跳队列时初始化心跳时间
            conn->setLastRecvHBTime(nowms);
        }
        
        if (nowms - conn->getLastRecvHBTime() > CORPC_MAX_NO_HEARTBEAT_TIME) {
            // 心跳超时，断线处理
            ERROR_LOG("Heartbeater::heartbeatRoutine() -- heartbeat timeout for conn: %lu fd %d\n", (uint64_t)conn.get(), conn->getfd());
            self->heartbeatList_.remove(node);
            conn->close();
            continue;
        }
        
        if (expireTime > nowms) {
            //if (expireTime - nowms > 100) {
            //    msleep(100);
            //} else {
            //    msleep(expireTime - nowms);
            //}
            msleep(expireTime - nowms);

            continue;
        }

        self->heartbeatList_.remove(node);
DEBUG_LOG("Heartbeater::heartbeatRoutine() -- send heartbeat for conn: %lu fd %d\n", (uint64_t)conn.get(), conn->getfd());
        // 发心跳包
        conn->send(self->heartbeatmsg_);

        // 重新加入队列
        // TODO: 不同连接允许不一样的心跳周期
        self->heartbeatList_.insert((uint64_t)conn.get(), nowms + CORPC_HEARTBEAT_PERIOD, conn);
    }
}

void Heartbeater::addConnection(std::shared_ptr<Connection>& connection) {
    HeartbeatTask *task = new HeartbeatTask;
    task->type = HeartbeatTask::START;
    task->connection = connection;
    queue_.push(task);
}

void Heartbeater::removeConnection(std::shared_ptr<Connection>& connection) {
    HeartbeatTask *task = new HeartbeatTask;
    task->type = HeartbeatTask::STOP;
    task->connection = connection;
    queue_.push(task);
}

IO::IO(uint16_t receiveThreadNum, uint16_t sendThreadNum): receiveThreadNum_(receiveThreadNum), sendThreadNum_(sendThreadNum) {
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
    if (sendThreadNum_ == 0 && receiveThreadNum_ == 0) {
        ERROR_LOG("IO::start() -- sender and receiver can't run at same thread.\n");
        return false;
    }
    
    // 根据需要启动receiver线程或协程
    if (receiveThreadNum_ > 0) {
        receiver_ = new MultiThreadReceiver(this, receiveThreadNum_);
    } else {
        receiver_ = new CoroutineReceiver(this);
    }
    
    // 根据需要启动sender线程或协程
    if (sendThreadNum_ > 0) {
        sender_ = new MultiThreadSender(this, sendThreadNum_);
    } else {
        sender_ = new CoroutineSender(this);
    }
    
    if (!receiver_->start()) {
        ERROR_LOG("IO::start() -- start receiver failed.\n");
        return false;
    }
    
    if (!sender_->start()) {
        ERROR_LOG("IO::start() -- start sender failed.\n");
        return false;
    }
    
    return true;
}

void IO::addConnection(std::shared_ptr<Connection>& connection) {
    // 注意：以下两行顺序不能调换，不然会有多线程问题
    sender_->addConnection(connection);
    receiver_->addConnection(connection);
}

void IO::removeConnection(std::shared_ptr<Connection>& connection) {
    sender_->removeConnection(connection);
}
