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
    class WorkerTask;
    
    // 接收数据和发送数据的pipeline流水线处理，流水线中的处理单元是有状态的，难点：1.流水线中的处理单元的处理数据类型 2.会增加内存分配和数据拷贝影响效率
    // 上流流水线处理流程：
    //   1.数据包分割：从buf中分隔出一个个的未decode的消息原始数据，输入是(uint8_t *)buf，输出是(uint8_t *)rawdata，该单元缓存数据状态
    //   2.解码消息：将原始数据解码成消息对象，输入是(uint8_t *)rawdata，输出是(std::shared_ptr<void>)msg，该单元无状态
    //   3.消息处理：将消息提交给worker处理
    // 下流流水线处理流程：
    //   1.编码器链：根据数据类型进行编码，从链头编码器开始，如果当前编码器认识要处理的数据类型则编码并返回，否则交由链中下一个编码器处理，直到有编码器可处理数据为止，若无编码器可处理数据，则报错
    
    typedef std::function<WorkerTask* (std::shared_ptr<Connection>&, uint8_t*, uint8_t*, int)> DecodeFunction;
    typedef std::function<bool (std::shared_ptr<Connection>&, std::shared_ptr<void>&, uint8_t*, int, int&, std::string&, uint32_t&)> EncodeFunction;
    
    typedef MPMC_NoLockBlockQueue<WorkerTask*> WorkerMessageQueue;
    
    class WorkerTask {
    public:
        WorkerTask() {}
        virtual ~WorkerTask() = 0;

        virtual void doTask() = 0; // 注意：处理完消息需要自己删除自己
    };

    class Worker {
    public:
        Worker() {}
        virtual ~Worker() = 0;
        
        virtual void start() = 0;
        
        void addTask(WorkerTask *task);
        
    protected:
        static void *taskHandleRoutine(void * arg);

    protected:
        WorkerMessageQueue queue_;
    };
    
    class MultiThreadWorker: public Worker {
    public:
        MultiThreadWorker(uint16_t threadNum): threadNum_(threadNum), ts_(threadNum) {}
        virtual ~MultiThreadWorker() {}
        
        virtual void start();
        
    protected:
        static void threadEntry( Worker *self );
        
    private:
        uint16_t threadNum_;
        std::vector<std::thread> ts_;
    };
    
    class CoroutineWorker: public Worker {
    public:
        CoroutineWorker() {}
        virtual ~CoroutineWorker() {}
        
        virtual void start();
    };
    
    class Pipeline {
    public:
        Pipeline(std::shared_ptr<Connection> &connection, Worker *worker);
        virtual ~Pipeline() = 0;
        
        virtual bool upflow(uint8_t *buf, int size) = 0;
        virtual bool downflow(uint8_t *buf, int space, int &size) = 0;
        
    protected:
        Worker *worker_;
        
        std::weak_ptr<Connection> connection_;
    };
    
    class MessagePipeline: public Pipeline {
    public:
        enum SIZE_TYPE { TWO_BYTES, FOUR_BYTES };
        
    public:
        MessagePipeline(std::shared_ptr<Connection> &connection, Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize);
        virtual ~MessagePipeline() = 0;
        
        virtual bool downflow(uint8_t *buf, int space, int &size) override final;
        
    protected:
        DecodeFunction decodeFun_;
        EncodeFunction encodeFun_;
        
        uint headSize_;
        uint maxBodySize_;
        
        std::string head_;
        uint8_t *headBuf_;
        
        std::string body_;
        uint8_t *bodyBuf_;
        uint bodySize_;
        
    private:
        std::string downflowBuf_; // 在downflow过程中写不进buf的数据将记录到_downflowBuf中
        uint32_t downflowBufSentNum_; // 已发送的数据量
    };
    
    class TcpPipeline: public MessagePipeline {
    public:
        TcpPipeline(std::shared_ptr<Connection> &connection, Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize, uint bodySizeOffset, SIZE_TYPE bodySizeType);
        virtual ~TcpPipeline() {}
        
        virtual bool upflow(uint8_t *buf, int size);
        
    private:
        uint headNum_;
        uint bodyNum_;
        
        uint bodySizeOffset_;
        SIZE_TYPE bodySizeType_;
    };
    
    class UdpPipeline: public MessagePipeline {
    public:
        UdpPipeline(std::shared_ptr<Connection> &connection, Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize);
        virtual ~UdpPipeline() {}
        
        virtual bool upflow(uint8_t *buf, int size);
    };
    
    class PipelineFactory {
    public:
        PipelineFactory(corpc::Worker *worker): worker_(worker) {}
        virtual ~PipelineFactory() = 0;
        
        virtual std::shared_ptr<Pipeline> buildPipeline(std::shared_ptr<Connection> &connection) = 0;
        
    protected:
        Worker *worker_;
    };
    
    class MessagePipelineFactory: public PipelineFactory {
    public:
        MessagePipelineFactory(corpc::Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize): PipelineFactory(worker), decodeFun_(decodeFun), encodeFun_(encodeFun), headSize_(headSize), maxBodySize_(maxBodySize) {}
        virtual ~MessagePipelineFactory() = 0;
        
        virtual std::shared_ptr<Pipeline> buildPipeline(std::shared_ptr<Connection> &connection) = 0;
        
    protected:
        DecodeFunction decodeFun_;
        EncodeFunction encodeFun_;
        
        uint headSize_;
        uint maxBodySize_;
    };
    
    class TcpPipelineFactory: public MessagePipelineFactory {
    public:
        TcpPipelineFactory(Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize, uint bodySizeOffset, MessagePipeline::SIZE_TYPE bodySizeType): MessagePipelineFactory(worker, decodeFun, encodeFun, headSize, maxBodySize), bodySizeOffset_(bodySizeOffset), bodySizeType_(bodySizeType) {}
        ~TcpPipelineFactory() {}
        
        virtual std::shared_ptr<Pipeline> buildPipeline(std::shared_ptr<Connection> &connection);
        
    public:
        uint bodySizeOffset_;
        MessagePipeline::SIZE_TYPE bodySizeType_;
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
        
        virtual void onConnect() = 0;
        virtual void onClose() = 0;
        virtual void cleanDataOnClosing(std::shared_ptr<void>& data) {}

        virtual void onSenderInit() {}
        virtual void onReceiverInit() {}
    public:
        void setPipeline(std::shared_ptr<Pipeline> &pipeline) { pipeline_ = pipeline; }
        std::shared_ptr<Pipeline> &getPipeline() { return pipeline_; }
        
        int getfd() { return fd_; }
        
        std::shared_ptr<Connection> getPtr() {
            return shared_from_this();
        }

        int getSendThreadIndex() { return sendThreadIndex_; }
        void setSendThreadIndex(int threadIndex) { sendThreadIndex_ = threadIndex; }
        int getRecvThreadIndex() { return recvThreadIndex_; }
        void setRecvThreadIndex(int threadIndex) { recvThreadIndex_ = threadIndex; }
        
        bool needHB() { return needHB_; }
        uint64_t getLastRecvHBTime() { return lastRecvHBTime_; }
        void setLastRecvHBTime(uint64_t time) { lastRecvHBTime_ = time; }
        
        bool isDecodeError() { return decodeError_; }
        void setDecodeError() { decodeError_ = true; }
        
        void send(std::shared_ptr<void> data);
        
        void close();
        
        size_t getDataSize() { return datas_.size(); }
        std::shared_ptr<void>& getFrontData() { return datas_.front(); }
        void popFrontData() { datas_.pop_front(); }
        
        bool isOpen() const { return !(isClosing_ || closed_); }
        bool isHBing() const { return isHBing_; }

        Mutex &getLock() { return lock_; }

    protected:
        virtual ssize_t write(const void *buf, size_t nbyte);
        
    protected:
        IO *io_;
        int fd_; // connect fd
        bool routineHang_; // 协程是否挂起
        stCoRoutine_t* routine_; // 协程
        
        int sendThreadIndex_; // 分配到sender的线程下标
        int recvThreadIndex_; // 分配到receiver的线程下标
        
        bool needHB_; // 是否进行心跳
        std::atomic<uint64_t> lastRecvHBTime_; // 最后一次收到数据的时间
        
        std::shared_ptr<Pipeline> pipeline_;
        std::list<std::shared_ptr<void>> datas_; // 等待发送的数据
        
        bool decodeError_; // 是否数据解码出错
        std::atomic<bool> closed_; // 是否已关闭
        std::atomic<bool> isClosing_; // 是否正在关闭
        std::atomic<bool> isHBing_; // 是否正在心跳

        Semaphore closeSem_; // 关闭同步用信号量（注意：应该用条件变量更合适）
        Mutex lock_; // 连接关闭同步锁
        //std::atomic<bool> canClose_; // 是否可调用close（当sender中fd相关协程退出时设置canClose为true，receiver中fd相关协程才可以进行close调用）
        
    public:
        friend class Receiver;
        friend class Sender;
        friend class Heartbeater;
    };
    
    class Acceptor;
    
    // 服务器基类
    class Server {
    public:
        Server(IO *io): io_(io), acceptor_(nullptr), worker_(nullptr), pipelineFactory_(nullptr) {}
        virtual ~Server() = 0;
        
        std::shared_ptr<Connection> buildAndAddConnection(int fd);
        
    protected:
        virtual bool start();
        
        virtual Connection *buildConnection(int fd) = 0;
        //virtual void onConnect(std::shared_ptr<Connection>& connection) = 0;
        //virtual void onClose(std::shared_ptr<Connection>& connection) = 0;
    protected:
        IO *io_;
        
        Acceptor *acceptor_;
        Worker *worker_;
        
        //PipelineFactory *pipelineFactory_;
        std::unique_ptr<PipelineFactory> pipelineFactory_;
    };
    
    class Acceptor {
    public:
        Acceptor(Server *server, const std::string& ip, uint16_t port);
        virtual ~Acceptor() = 0;
        
        virtual bool start() = 0;
        
    protected:
        Server *server_;
        
        std::string ip_;
        uint16_t port_;
        
        sockaddr_in local_addr_;
        
        int listen_fd_;
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
        int shake_fd_;
        std::thread t_;
        
        std::string shakemsg2_;
        uint8_t *shakemsg2buf_;
        std::string shakemsg4_;
        uint8_t *shakemsg4buf_;
        std::string unshakemsg_;
        uint8_t *unshakemsg2buf_;

        std::map<sockaddr_in, bool, SockAddrCmp> shakingClient_;
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
            //Receiver *receiver_;
            
            // 消息队列
            ReceiverTaskQueue queue_;
        };
        
    public:
        Receiver(IO *io):io_(io) {}
        virtual ~Receiver() = 0;
        
        virtual bool start() = 0;
        
        virtual void addConnection(std::shared_ptr<Connection>& connection) = 0;
        
    protected:
        static void *connectionDispatchRoutine( void * arg );
        
        static void *connectionRoutine( void * arg );
        
    protected:
        IO *io_;
    };
    
    class MultiThreadReceiver: public Receiver {
        // 线程相关数据
        struct ThreadData {
            QueueContext queueContext_;
            
            // 保持thread对象
            std::thread t_;
        };
        
    public:
        MultiThreadReceiver(IO *io, uint16_t threadNum): Receiver(io), threadNum_(threadNum), lastThreadIndex_(0), threadDatas_(threadNum) {}
        virtual ~MultiThreadReceiver() {}
        
        virtual bool start();
        
        virtual void addConnection(std::shared_ptr<Connection>& connection);
        
    protected:
        static void threadEntry( ThreadData *tdata );
        
    private:
        uint16_t threadNum_;
        std::atomic<uint16_t> lastThreadIndex_;
        std::vector<ThreadData> threadDatas_;
    };
    
    class CoroutineReceiver: public Receiver {
    public:
        CoroutineReceiver(IO *io): Receiver(io) { /*queueContext_.receiver_ = this;*/ }
        virtual ~CoroutineReceiver() {}
        
        virtual bool start();
        
        virtual void addConnection(std::shared_ptr<Connection>& connection);
        
    private:
        QueueContext queueContext_;
    };
    
    // Sender负责rpc连接的数据发送
    class Sender {
    protected:
        struct QueueContext {
            //Sender *sender_;
            
            // 消息队列
            SenderTaskQueue queue_;
        };
        
    public:
        Sender(IO *io):io_(io) {}
        virtual ~Sender() = 0;
        
        virtual bool start() = 0;
        
        virtual void addConnection(std::shared_ptr<Connection>& connection) = 0;
        virtual void removeConnection(std::shared_ptr<Connection>& connection) = 0;
        virtual void send(std::shared_ptr<Connection>& connection, std::shared_ptr<void> data) = 0;
    protected:
        static void *taskQueueRoutine( void * arg );
        static void *connectionRoutine( void * arg );
        
    private:
        IO *io_;
    };
    
    class MultiThreadSender: public Sender {
        // 线程相关数据
        struct ThreadData {
            QueueContext queueContext_;
            
            // 保持thread对象
            std::thread t_;
        };
        
    public:
        MultiThreadSender(IO *io, uint16_t threadNum): Sender(io), threadNum_(threadNum), lastThreadIndex_(0), threadDatas_(threadNum) {}
        virtual ~MultiThreadSender() {}
        
        virtual bool start();
        
        virtual void addConnection(std::shared_ptr<Connection>& connection);
        virtual void removeConnection(std::shared_ptr<Connection>& connection);
        virtual void send(std::shared_ptr<Connection>& connection, std::shared_ptr<void> data);
    private:
        static void threadEntry( ThreadData *tdata );
        
    private:
        uint16_t threadNum_;
        std::atomic<uint16_t> lastThreadIndex_;
        std::vector<ThreadData> threadDatas_;
    };
    
    class CoroutineSender: public Sender {
    public:
        CoroutineSender(IO *io): Sender(io) { /*queueContext_.sender_ = this;*/ }
        virtual ~CoroutineSender() {}
        
        virtual bool start();
        
        virtual void addConnection(std::shared_ptr<Connection>& connection);
        virtual void removeConnection(std::shared_ptr<Connection>& connection);
        virtual void send(std::shared_ptr<Connection>& connection, std::shared_ptr<void> data);
    private:
        QueueContext queueContext_;
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
        ~Heartbeater() { t_.detach(); }
        
        static void threadEntry( Heartbeater *self );
        
        static void *dispatchRoutine( void * arg ); // 负责接收新心跳任务
        static void *heartbeatRoutine( void * arg ); // 负责对握过手的连接进行心跳
        
    private:
        std::shared_ptr<SendMessageInfo> heartbeatmsg_;
        
        HeartbeatQueue queue_;
        std::thread t_;
        
        TimeoutList<std::shared_ptr<Connection>> heartbeatList_;
        
        bool heartbeatRoutineHang_; // 心跳协程是否挂起
        stCoRoutine_t* heartbeatRoutine_; // 心跳协程
        
    };
    
    // 注意：
    // 1. IO对象不支持释放销毁，因为线程的协程运行环境不支持销毁（即带协程环境的线程启动后就一直运行）
    //    由于receiver、sender和worker都是可自带线程的，因此也不能销毁
    // 2. 当threadNum为0时表示在当前线程中启动处理协程
    // 3. sendThreadNum和receiveThreadNum不能同为0，因为同一线程中一个fd不能同时在两个协程中进行处理，会触发EEXIST错误
    class IO {
    public:
        static IO* create(uint16_t receiveThreadNum, uint16_t sendThreadNum, uint16_t workThreadNum);
        
        bool start();
        
        void destroy() { delete this; } // 销毁IO
        
        Receiver *getReceiver() { return receiver_; }
        Sender *getSender() { return sender_; }
        Worker *getWorker() { return worker_; }
        
        void addConnection(std::shared_ptr<Connection>& connection);
        void removeConnection(std::shared_ptr<Connection>& connection);
        
    private:
        IO(uint16_t receiveThreadNum, uint16_t sendThreadNum, uint16_t workThreadNum);
        ~IO() {}  // 不允许在栈上创建IO
        
    private:
        uint16_t receiveThreadNum_;
        uint16_t sendThreadNum_;
        uint16_t workThreadNum_;
        
        Receiver *receiver_;
        Sender *sender_;
        Worker *worker_;
        
    public:
        friend class Connection;
        friend class Receiver;
        friend class Sender;
    };
    
}

#endif /* corpc_io_h */
