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

#ifndef corpc_io_h
#define corpc_io_h

#include "co_routine.h"
#include "corpc_define.h"
#include "corpc_queue.h"
#include "corpc_timeout_list.h"
#include <functional>

#include <thread>

// 注意：当前UDP绑定四元组的实现方式还有问题，消息还是会被没绑定四元组的socket接收，有时又能正确发到绑定四元组的socket上，未找到具体原因（好像和系统有关系）
namespace corpc {
    
    class IO;
    class Receiver;
    class Sender;
    class Connection;
    class Pipeline;
    class Heartbeater;
    
    // 接收数据和发送数据的pipeline流水线处理，流水线中的处理单元是有状态的，难点：1.流水线中的处理单元的处理数据类型 2.会增加内存分配和数据拷贝影响效率
    // 上流流水线处理流程：
    //   1.数据包分割：从buf中分隔出一个个的未decode的消息原始数据，输入是(uint8_t *)buf，输出是(uint8_t *)rawdata，该单元缓存数据状态
    //   2.解码消息：将原始数据解码成消息对象，输入是(uint8_t *)rawdata，输出是(std::shared_ptr<void>)msg，该单元无状态
    //   3.消息处理：将消息提交给worker处理
    // 下流流水线处理流程：
    //   1.编码器链：根据数据类型进行编码，从链头编码器开始，如果当前编码器认识要处理的数据类型则编码并返回，否则交由链中下一个编码器处理，直到有编码器可处理数据为止，若无编码器可处理数据，则报错
    
    typedef std::function<void* (std::shared_ptr<Connection>&, uint8_t*, uint8_t*, int)> DecodeFunction;
    typedef std::function<bool (std::shared_ptr<Connection>&, std::shared_ptr<void>&, uint8_t*, int, int&, std::string&, uint32_t&)> EncodeFunction;
    
    typedef MPMC_NoLockBlockQueue<void*> WorkerMessageQueue;
    
    class Worker {
    public:
        Worker() {}
        virtual ~Worker() = 0;
        
        virtual void start() = 0;
        
        void addMessage(void *msg);
        
    protected:
        static void *msgHandleRoutine(void * arg);
        
        virtual void handleMessage(void *msg) = 0; // 注意：处理完消息需要自己删除msg

    protected:
        WorkerMessageQueue _queue;
    };
    
    class MultiThreadWorker: public Worker {
    public:
        MultiThreadWorker(uint16_t threadNum): _threadNum(threadNum), _ts(threadNum) {}
        virtual ~MultiThreadWorker() = 0;
        
        virtual void start();
        
    protected:
        static void threadEntry( Worker *self );
        
        virtual void handleMessage(void *msg) = 0; // 注意：处理完消息需要自己删除msg
        
    private:
        uint16_t _threadNum;
        std::vector<std::thread> _ts;
    };
    
    class CoroutineWorker: public Worker {
    public:
        CoroutineWorker() {}
        virtual ~CoroutineWorker() = 0;
        
        virtual void start();
        
    protected:
        virtual void handleMessage(void *msg) = 0; // 注意：处理完消息需要自己删除msg
    };
    
    class Pipeline {
    public:
        Pipeline(std::shared_ptr<Connection> &connection, Worker *worker);
        virtual ~Pipeline() = 0;
        
        virtual bool upflow(uint8_t *buf, int size) = 0;
        virtual bool downflow(uint8_t *buf, int space, int &size) = 0;
        
    protected:
        Worker *_worker;
        
        std::weak_ptr<Connection> _connection;
    };
    
    class MessagePipeline: public Pipeline {
    public:
        enum SIZE_TYPE { TWO_BYTES, FOUR_BYTES };
        
    public:
        MessagePipeline(std::shared_ptr<Connection> &connection, Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize);
        virtual ~MessagePipeline() = 0;
        
        virtual bool downflow(uint8_t *buf, int space, int &size) override final;
        
    protected:
        DecodeFunction _decodeFun;
        EncodeFunction _encodeFun;
        
        uint _headSize;
        uint _maxBodySize;
        
        std::string _head;
        uint8_t *_headBuf;
        
        std::string _body;
        uint8_t *_bodyBuf;
        uint _bodySize;
        
    private:
        std::string _downflowBuf; // 在downflow过程中写不进buf的数据将记录到_downflowBuf中
        uint32_t _downflowBufSentNum; // 已发送的数据量
    };
    
    class TcpPipeline: public MessagePipeline {
    public:
        TcpPipeline(std::shared_ptr<Connection> &connection, Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize, uint bodySizeOffset, SIZE_TYPE bodySizeType);
        virtual ~TcpPipeline() {}
        
        virtual bool upflow(uint8_t *buf, int size);
        
    private:
        uint _headNum;
        uint _bodyNum;
        
        uint _bodySizeOffset;
        SIZE_TYPE _bodySizeType;
    };
    
    class UdpPipeline: public MessagePipeline {
    public:
        UdpPipeline(std::shared_ptr<Connection> &connection, Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize);
        virtual ~UdpPipeline() {}
        
        virtual bool upflow(uint8_t *buf, int size);
    };
    
    class PipelineFactory {
    public:
        PipelineFactory(corpc::Worker *worker): _worker(worker) {}
        virtual ~PipelineFactory() = 0;
        
        virtual std::shared_ptr<Pipeline> buildPipeline(std::shared_ptr<Connection> &connection) = 0;
        
    protected:
        Worker *_worker;
    };
    
    class MessagePipelineFactory: public PipelineFactory {
    public:
        MessagePipelineFactory(corpc::Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize): PipelineFactory(worker), _decodeFun(decodeFun), _encodeFun(encodeFun), _headSize(headSize), _maxBodySize(maxBodySize) {}
        virtual ~MessagePipelineFactory() = 0;
        
        virtual std::shared_ptr<Pipeline> buildPipeline(std::shared_ptr<Connection> &connection) = 0;
        
    protected:
        DecodeFunction _decodeFun;
        EncodeFunction _encodeFun;
        
        uint _headSize;
        uint _maxBodySize;
    };
    
    class TcpPipelineFactory: public MessagePipelineFactory {
    public:
        TcpPipelineFactory(Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize, uint bodySizeOffset, MessagePipeline::SIZE_TYPE bodySizeType): MessagePipelineFactory(worker, decodeFun, encodeFun, headSize, maxBodySize), _bodySizeOffset(bodySizeOffset), _bodySizeType(bodySizeType) {}
        ~TcpPipelineFactory() {}
        
        virtual std::shared_ptr<Pipeline> buildPipeline(std::shared_ptr<Connection> &connection);
        
    public:
        uint _bodySizeOffset;
        MessagePipeline::SIZE_TYPE _bodySizeType;
    };
    
    class UdpPipelineFactory: public MessagePipelineFactory {
    public:
        UdpPipelineFactory(Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize): MessagePipelineFactory(worker, decodeFun, encodeFun, headSize, maxBodySize) {}
        ~UdpPipelineFactory() {}
        
        virtual std::shared_ptr<Pipeline> buildPipeline(std::shared_ptr<Connection> &connection);
    };
    
    class Connection: public std::enable_shared_from_this<Connection> {
    public:
        Connection(int fd, IO* io, bool needHB);
        virtual ~Connection() = 0;
        
        virtual void onClose() = 0;
        virtual void cleanDataOnClosing(std::shared_ptr<void>& data) {}

        virtual void onSenderInit() {}
        virtual void onReceiverInit() {}
    public:
        void setPipeline(std::shared_ptr<Pipeline> &pipeline) { _pipeline = pipeline; }
        std::shared_ptr<Pipeline> &getPipeline() { return _pipeline; }
        
        int getfd() { return _fd; }
        
        std::shared_ptr<Connection> getPtr() {
            return shared_from_this();
        }

        int getSendThreadIndex() { return _sendThreadIndex; }
        void setSendThreadIndex(int threadIndex) { _sendThreadIndex = threadIndex; }
        int getRecvThreadIndex() { return _recvThreadIndex; }
        void setRecvThreadIndex(int threadIndex) { _recvThreadIndex = threadIndex; }
        
        bool needHB() { return _needHB; }
        uint64_t getLastRecvHBTime() { return _lastRecvHBTime; }
        void setLastRecvHBTime(uint64_t time) { _lastRecvHBTime = time; }
        
        bool isDecodeError() { return _decodeError; }
        void setDecodeError() { _decodeError = true; }
        
        void send(std::shared_ptr<void> data);
        
        void close();
        
        size_t getDataSize() { return _datas.size(); }
        std::shared_ptr<void>& getFrontData() { return _datas.front(); }
        void popFrontData() { _datas.pop_front(); }
        
        bool isOpen() const { return !(_isClosing || _closed); }

    protected:
        virtual ssize_t write(const void *buf, size_t nbyte);
        
    protected:
        IO *_io;
        int _fd; // connect fd
        bool _routineHang; // 协程是否挂起
        stCoRoutine_t* _routine; // 协程
        
        int _sendThreadIndex; // 分配到sender的线程下标
        int _recvThreadIndex; // 分配到receiver的线程下标
        
        bool _needHB; // 是否进行心跳
        std::atomic<uint64_t> _lastRecvHBTime; // 最后一次收到数据的时间
        
        std::shared_ptr<Pipeline> _pipeline;
        std::list<std::shared_ptr<void>> _datas; // 等待发送的数据
        
        bool _decodeError; // 是否数据解码出错
        std::atomic<bool> _closed; // 是否已关闭
        std::atomic<bool> _isClosing; // 是否正在关闭
        std::atomic<bool> _canClose; // 是否可调用close（当sender中fd相关协程退出时设置canClose为true，receiver中fd相关协程才可以进行close调用）
        
    public:
        friend class Receiver;
        friend class Sender;
        friend class Heartbeater;
    };
    
    class Acceptor;
    
    // 服务器基类
    class Server {
    public:
        Server(IO *io): _io(io), _acceptor(nullptr), _worker(nullptr), _pipelineFactory(nullptr) {}
        virtual ~Server() = 0;
        
        std::shared_ptr<Connection> buildAndAddConnection(int fd);
        
    protected:
        virtual bool start();
        
        virtual Connection * buildConnection(int fd) = 0;
        virtual void onConnect(std::shared_ptr<Connection>& connection) = 0;
        virtual void onClose(std::shared_ptr<Connection>& connection) = 0;
    protected:
        IO *_io;
        
        Acceptor *_acceptor;
        Worker *_worker;
        
        PipelineFactory *_pipelineFactory;
    };
    
    class Acceptor {
    public:
        Acceptor(Server *server, const std::string& ip, uint16_t port);
        virtual ~Acceptor() = 0;
        
        virtual bool start() = 0;
        
    protected:
        Server *_server;
        
        std::string _ip;
        uint16_t _port;
        
        sockaddr_in _local_addr;
        
        int _listen_fd;
    };
    
    class TcpAcceptor: public Acceptor {
    public:
        TcpAcceptor(Server *server, const std::string& ip, uint16_t port): Acceptor(server, ip, port) {}
        virtual ~TcpAcceptor() {}
        
        virtual bool start();
        
    private:
        static void *acceptRoutine( void * arg );
        
    };

    class SockAddrCmp: public std::less<sockaddr_in> {
    public:
        bool operator()(const sockaddr_in& s1, const sockaddr_in& s2){
            if (s1.sin_addr.s_addr != s2.sin_addr.s_addr) {
                return s1.sin_addr.s_addr < s2.sin_addr.s_addr;
            }

            if (s1.sin_port != s2.sin_port) {
                return s1.sin_port < s2.sin_port;
            }

            return false;
        }
    };
    
    class UdpAcceptor: public Acceptor {
        struct HandshakeInfo {
            UdpAcceptor *self;
            
            sockaddr_in addr;
        };

    public:
        UdpAcceptor(Server *server, const std::string& ip, uint16_t port);
        virtual ~UdpAcceptor() {}
        
        virtual bool start();
        
    private:
        // 启动新线程来负责连接握手
        static void threadEntry( UdpAcceptor *self );
        
        static void *acceptRoutine( void * arg ); // 负责监听新连接
        static void *handshakeRoutine( void * arg ); // 负责新连接握手
        
    private:
        int _shake_fd;
        std::thread _t;
        
        std::string _shakemsg2;
        uint8_t *_shakemsg2buf;
        std::string _shakemsg4;
        uint8_t *_shakemsg4buf;
        std::string _unshakemsg;
        uint8_t *_unshakemsg2buf;

        std::map<sockaddr_in, bool, SockAddrCmp> _shakingClient;
    };
    
    struct SenderTask {
        enum TaskType {INIT, CLOSE, DATA};
        std::shared_ptr<Connection> connection;
        TaskType type;
        std::shared_ptr<void> data;
    };
    
    struct ReceiverTask {
        std::shared_ptr<Connection> connection;
    };

    struct HeartbeatTask {
        enum TaskType {START, STOP};
        TaskType type;
        std::shared_ptr<Connection> connection;
    };
    
#ifdef USE_NO_LOCK_QUEUE
    typedef Co_MPSC_NoLockQueue<ReceiverTask*> ReceiverTaskQueue; // 用于从Acceptor向Receiver传递建立的连接fd
    typedef Co_MPSC_NoLockQueue<SenderTask*> SenderTaskQueue; // 用于向Sender发送任务
    typedef Co_MPSC_NoLockQueue<HeartbeatTask*> HeartbeatQueue; // 用于向Heartbeater发送需要心跳的连接
#else
    typedef CoSyncQueue<ReceiverTask*> ReceiverTaskQueue; // 用于从Acceptor向Receiver传递建立的连接fd
    typedef CoSyncQueue<SenderTask*> SenderTaskQueue; // 用于向Sender发送任务
    typedef CoSyncQueue<HeartbeatTask*> HeartbeatQueue; // 用于向Heartbeater发送需要心跳的连接
#endif
    
    // Receiver负责rpc连接的数据接受
    class Receiver {
    protected:
        struct QueueContext {
            Receiver *_receiver;
            
            // 消息队列
            ReceiverTaskQueue _queue;
        };
        
    public:
        Receiver(IO *io):_io(io) {}
        virtual ~Receiver() = 0;
        
        virtual bool start() = 0;
        
        virtual void addConnection(std::shared_ptr<Connection>& connection) = 0;
        
    protected:
        static void *connectionDispatchRoutine( void * arg );
        
        static void *connectionRoutine( void * arg );
        
    protected:
        IO *_io;
    };
    
    class MultiThreadReceiver: public Receiver {
        // 线程相关数据
        struct ThreadData {
            QueueContext _queueContext;
            
            // 保持thread对象
            std::thread _t;
        };
        
    public:
        MultiThreadReceiver(IO *io, uint16_t threadNum): Receiver(io), _threadNum(threadNum), _lastThreadIndex(0), _threadDatas(threadNum) {}
        virtual ~MultiThreadReceiver() {}
        
        virtual bool start();
        
        virtual void addConnection(std::shared_ptr<Connection>& connection);
        
    protected:
        static void threadEntry( ThreadData *tdata );
        
    private:
        uint16_t _threadNum;
        std::atomic<uint16_t> _lastThreadIndex;
        std::vector<ThreadData> _threadDatas;
    };
    
    class CoroutineReceiver: public Receiver {
    public:
        CoroutineReceiver(IO *io): Receiver(io) { _queueContext._receiver = this; }
        virtual ~CoroutineReceiver() {}
        
        virtual bool start();
        
        virtual void addConnection(std::shared_ptr<Connection>& connection);
        
    private:
        QueueContext _queueContext;
    };
    
    // Sender负责rpc连接的数据发送
    class Sender {
    protected:
        struct QueueContext {
            Sender *_sender;
            
            // 消息队列
            SenderTaskQueue _queue;
        };
        
    public:
        Sender(IO *io):_io(io) {}
        virtual ~Sender() = 0;
        
        virtual bool start() = 0;
        
        virtual void addConnection(std::shared_ptr<Connection>& connection) = 0;
        virtual void removeConnection(std::shared_ptr<Connection>& connection) = 0;
        virtual void send(std::shared_ptr<Connection>& connection, std::shared_ptr<void> data) = 0;
    protected:
        static void *taskQueueRoutine( void * arg );
        static void *connectionRoutine( void * arg );
        
    private:
        IO *_io;
    };
    
    class MultiThreadSender: public Sender {
        // 线程相关数据
        struct ThreadData {
            QueueContext _queueContext;
            
            // 保持thread对象
            std::thread _t;
        };
        
    public:
        MultiThreadSender(IO *io, uint16_t threadNum): Sender(io), _threadNum(threadNum), _lastThreadIndex(0), _threadDatas(threadNum) {}
        virtual ~MultiThreadSender() {}
        
        virtual bool start();
        
        virtual void addConnection(std::shared_ptr<Connection>& connection);
        virtual void removeConnection(std::shared_ptr<Connection>& connection);
        virtual void send(std::shared_ptr<Connection>& connection, std::shared_ptr<void> data);
    private:
        static void threadEntry( ThreadData *tdata );
        
    private:
        uint16_t _threadNum;
        std::atomic<uint16_t> _lastThreadIndex;
        std::vector<ThreadData> _threadDatas;
    };
    
    class CoroutineSender: public Sender {
    public:
        CoroutineSender(IO *io): Sender(io) { _queueContext._sender = this; }
        virtual ~CoroutineSender() {}
        
        virtual bool start();
        
        virtual void addConnection(std::shared_ptr<Connection>& connection);
        virtual void removeConnection(std::shared_ptr<Connection>& connection);
        virtual void send(std::shared_ptr<Connection>& connection, std::shared_ptr<void> data);
    private:
        QueueContext _queueContext;
    };
    
    // singleton
    class Heartbeater {
    public:
        static Heartbeater& Instance() {
            static Heartbeater heartbeater;
            
            return heartbeater;
        }
        
        void addConnection(std::shared_ptr<Connection>& connection);
        void removeConnection(std::shared_ptr<Connection>& connection);
        
    private:
        Heartbeater();
        Heartbeater(Heartbeater const&);
        Heartbeater& operator=(Heartbeater const&);
        ~Heartbeater() { _t.detach(); }
        
        static void threadEntry( Heartbeater *self );
        
        static void *dispatchRoutine( void * arg ); // 负责接收新心跳任务
        static void *heartbeatRoutine( void * arg ); // 负责对握过手的连接进行心跳
        
    private:
        std::shared_ptr<SendMessageInfo> _heartbeatmsg;
        
        HeartbeatQueue _queue;
        std::thread _t;
        
        TimeoutList<std::shared_ptr<Connection>> _heartbeatList;
        
        bool _heartbeatRoutineHang; // 心跳协程是否挂起
        stCoRoutine_t* _heartbeatRoutine; // 心跳协程
        
    };
    
    class IO {
    public:
        static IO* create(uint16_t receiveThreadNum, uint16_t sendThreadNum);
        
        bool start();
        
        void destroy() { delete this; } // 销毁IO
        
        Receiver *getReceiver() { return _receiver; }
        Sender *getSender() { return _sender; }
        
        void addConnection(std::shared_ptr<Connection>& connection);
        void removeConnection(std::shared_ptr<Connection>& connection);
        
    private:
        // 注意：sendThreadNum和receiveThreadNum不能同为0，因为同一线程中一个fd不能同时在两个协程中进行处理，会触发EEXIST错误
        IO(uint16_t receiveThreadNum, uint16_t sendThreadNum);
        ~IO() {}  // 不允许在栈上创建IO
        
    private:
        uint16_t _receiveThreadNum;
        uint16_t _sendThreadNum;
        
        Receiver *_receiver;
        Sender *_sender;
        
    public:
        friend class Connection;
        friend class Receiver;
        friend class Sender;
    };
    
}

#endif /* corpc_io_h */
