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
//#include "corpc_pipeline.h"

#include <thread>

namespace CoRpc {
    
    class IO;
    class Receiver;
    class Sender;
    class Connection;
    class Pipeline;
    
    // 接收数据和发送数据的pipeline流水线处理，流水线中的处理单元是有状态的，难点：1.流水线中的处理单元的处理数据类型 2.会增加内存分配和数据拷贝影响效率
    // 上流流水线处理流程：
    //   1.数据包分割：从buf中分隔出一个个的未decode的消息原始数据，输入是(uint8_t *)buf，输出是(uint8_t *)rawdata，该单元缓存数据状态
    //   2.解码消息：将原始数据解码成消息对象，输入是(uint8_t *)rawdata，输出是(std::shared_ptr<void>)msg，该单元无状态
    //   3.消息路由：根据消息类型将消息放到对应的消息队列中，该单元无状态
    // 下流流水线处理流程：
    //   1.编码器链：根据数据类型进行编码，从链头编码器开始，如果当前编码器认识要处理的数据类型则编码并返回，否则交由链中下一个编码器处理，直到有编码器可处理数据为止，若无编码器可处理数据，则报错
    
    class Decoder {
    public:
        Decoder() {}
        virtual ~Decoder() = 0;
        
        virtual void * decode(std::shared_ptr<Connection> &connection, uint8_t *head, uint8_t *body, int size) = 0;
    };
    
    class Router {
    public:
        Router() {}
        virtual ~Router() = 0;
        
        virtual void route(std::shared_ptr<Connection> &connection, void *msg) = 0;
    };
    
    class Encoder {
    public:
        Encoder() {}
        virtual ~Encoder() = 0;
        
        virtual bool encode(std::shared_ptr<Connection> &connection, std::shared_ptr<void> &data, uint8_t *buf, int space, int &size) = 0;
    };
    
    class Pipeline {
    public:
        enum SIZE_TYPE { TWO_BYTES, FOUR_BYTES };
        
    public:
        Pipeline(std::shared_ptr<Connection> &connection, uint headSize, uint maxBodySize);
        virtual ~Pipeline() = 0;
        
        virtual bool upflow(uint8_t *buf, int size) = 0;
        bool downflow(uint8_t *buf, int space, int &size);
        
        void setDecoder(std::shared_ptr<Decoder>& decoder) { _decoder = decoder; }
        std::shared_ptr<Decoder>& getDecoder() { return _decoder; }
        void setRouter(std::shared_ptr<Router>& router) { _router = router; }
        std::shared_ptr<Router>& getRouter() { return _router; }
        void addEncoder(std::shared_ptr<Encoder>& encoder) { _encoders.push_back(encoder); }
        
    protected:
        uint _headSize;
        uint _maxBodySize;
        
        std::string _head;
        uint8_t *_headBuf;
        
        std::string _body;
        uint8_t *_bodyBuf;
        uint _bodySize;
        
        std::shared_ptr<Decoder> _decoder;
        std::shared_ptr<Router> _router;
        std::vector<std::shared_ptr<Encoder>> _encoders;
        
        std::weak_ptr<Connection> _connection;
    };
    
    class TcpPipeline: public Pipeline {
    public:
        TcpPipeline(std::shared_ptr<Connection> &connection, uint headSize, uint maxBodySize, uint bodySizeOffset, SIZE_TYPE bodySizeType);
        virtual ~TcpPipeline() {}
        
        virtual bool upflow(uint8_t *buf, int size);
        
    private:
        uint _headNum;
        uint _bodyNum;
        
        uint _bodySizeOffset;
        SIZE_TYPE _bodySizeType;
    };
    
    class UdpPipeline: public Pipeline {
    public:
        UdpPipeline(std::shared_ptr<Connection> &connection, uint headSize, uint maxBodySize);
        virtual ~UdpPipeline() {}
        
        virtual bool upflow(uint8_t *buf, int size);
    };
    
    class PipelineFactory {
    public:
        PipelineFactory() {}
        virtual ~PipelineFactory() = 0;
        
        virtual std::shared_ptr<Pipeline> buildPipeline(std::shared_ptr<Connection> &connection) = 0;
    };
    
    class Connection: public std::enable_shared_from_this<Connection> {
    public:
        Connection(int fd, IO* io);
        virtual ~Connection() = 0;
        
        virtual void onClose() = 0;
    public:
        void setPipeline(std::shared_ptr<Pipeline> &pipeline) { _pipeline = pipeline; }
        std::shared_ptr<Pipeline> &getPipeline() { return _pipeline; }
        
        int getfd() { return _fd; }
        
        int getSendThreadIndex() { return _sendThreadIndex; }
        void setSendThreadIndex(int threadIndex) { _sendThreadIndex = threadIndex; }
        int getRecvThreadIndex() { return _recvThreadIndex; }
        void setRecvThreadIndex(int threadIndex) { _recvThreadIndex = threadIndex; }
        
        void send(std::shared_ptr<void> data);
    protected:
        IO *_io;
        int _fd; // connect fd
        bool _routineHang; // deamon协程是否挂起
        stCoRoutine_t* _routine; // deamon协程
        
        int _sendThreadIndex; // 分配到sender的线程下标
        int _recvThreadIndex; // 分配到receiver的线程下标
        
        std::shared_ptr<Pipeline> _pipeline;
        std::list<std::shared_ptr<void>> _datas; // 等待发送的数据
        
        std::atomic<bool> _isClosing; // 是否正在关闭
        std::atomic<bool> _canClose; // 是否可调用close（当sender中fd相关协程退出时设置canClose为true，receiver中fd相关协程才可以进行close调用）
        
    public:
        friend class Pipeline;
        friend class Receiver;
        friend class Sender;
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
    
#ifdef USE_NO_LOCK_QUEUE
    typedef Co_MPSC_NoLockQueue<ReceiverTask*> ReceiverTaskQueue; // 用于从Acceptor向Receiver传递建立的连接fd
    typedef Co_MPSC_NoLockQueue<SenderTask*> SenderTaskQueue; // 用于向sender发送任务
#else
    typedef CoSyncQueue<ReceiverTask*> ReceiverTaskQueue; // 用于从Acceptor向Receiver传递建立的连接fd
    typedef CoSyncQueue<SenderTask*> SenderTaskQueue; // 用于向sender发送任务
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
        MultiThreadReceiver(IO *io, uint16_t threadNum): Receiver(io), _threadNum(threadNum), _threadDatas(threadNum) {}
        virtual ~MultiThreadReceiver() {}
        
        virtual bool start();
        
        virtual void addConnection(std::shared_ptr<Connection>& connection);
        
    protected:
        static void threadEntry( ThreadData *tdata );
        
    private:
        uint16_t _threadNum;
        uint16_t _lastThreadIndex;
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
        MultiThreadSender(IO *io, uint16_t threadNum): Sender(io), _threadNum(threadNum), _threadDatas(threadNum) {}
        virtual ~MultiThreadSender() {}
        
        virtual bool start();
        
        virtual void addConnection(std::shared_ptr<Connection>& connection);
        virtual void removeConnection(std::shared_ptr<Connection>& connection);
        virtual void send(std::shared_ptr<Connection>& connection, std::shared_ptr<void> data);
    private:
        static void threadEntry( ThreadData *tdata );
        
    private:
        uint16_t _threadNum;
        uint16_t _lastThreadIndex;
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
    
    class IO {
    public:
        static IO* create(uint16_t receiveThreadNum, uint16_t sendThreadNum);
        
        bool start();
        
        void destroy() { delete this; } // 销毁IO
        
        Receiver *getReceiver() { return _receiver; }
        Sender *getSender() { return _sender; }
        
        void addConnection(std::shared_ptr<Connection>& connection);
        
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
