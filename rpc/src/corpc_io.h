//
//  corpc_io.h
//  rpcsvr
//
//  Created by Xianke Liu on 2018/2/23.
//Copyright © 2018年 Dena. All rights reserved.
//

#ifndef corpc_io_h
#define corpc_io_h

#include "co_routine.h"
#include "corpc_inner.h"

#include <thread>

#define CORPC_MAX_BUFFER_SIZE 0x100000

namespace CoRpc {
    
    class IO {
        
        class Receiver;
        class Sender;
        
    public:
        class Connection {
        public:
            Connection(int fd, IO* io);
            virtual ~Connection() = 0;
            
            // 接收数据处理，必须把buf中的数据处理完，返回成功或失败，失败时会导致断开连接
            virtual bool parseData(uint8_t *buf, int size) = 0;
            
            // 发送数据处理，将要发送的数据写入buf中，返回实际写入的数据量，不能大于space
            virtual int buildData(uint8_t *buf, int space) = 0;
        public:
            int getfd() { return _fd; }
            
            int getSendThreadIndex() { return _sendThreadIndex; }
            void setSendThreadIndex(int threadIndex) { _sendThreadIndex = threadIndex; }
            int getRecvThreadIndex() { return _recvThreadIndex; }
            void setRecvThreadIndex(int threadIndex) { _recvThreadIndex = threadIndex; }
            
            IO *getIO() { return _io; }
        protected:
            IO *_io;
            int _fd; // connect fd
            bool _routineHang; // deamon协程是否挂起
            stCoRoutine_t* _routine; // deamon协程
            
            int _sendThreadIndex; // 分配到sender的线程下标
            int _recvThreadIndex; // 分配到receiver的线程下标
            
            std::list<std::shared_ptr<void>> _datas; // 等待发送的数据
            
            std::atomic<bool> _isClosing; // 是否正在关闭
            std::atomic<bool> _canClose; // 是否可调用close（当sender中fd相关协程退出时设置canClose为true，receiver中fd相关协程才可以进行close调用）
            
        public:
            friend class Receiver;
            friend class Sender;
        };
        
    private:
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
        typedef Co_MPSC_NoLockQueue<ReceiverTask*, static_cast<struct ReceiverTask *>(NULL)> ReceiverTaskQueue; // 用于从Acceptor向Receiver传递建立的连接fd
        typedef Co_MPSC_NoLockQueue<SenderTask*, static_cast<struct SenderTask *>(NULL)> SenderTaskQueue; // 用于向sender发送任务
#else
        typedef CoSyncQueue<ReceiverTask*, static_cast<struct ReceiverTask *>(NULL)> ReceiverTaskQueue; // 用于从Acceptor向Receiver传递建立的连接fd
        typedef CoSyncQueue<SenderTask*, static_cast<struct SenderTask *>(NULL)> SenderTaskQueue; // 用于向sender发送任务
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
            
            virtual void addConnection(std::shared_ptr<Connection> connection) = 0;
            
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
            
            bool start();
            
            void addConnection(std::shared_ptr<Connection> connection);
            
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
            
            bool start();
            
            void addConnection(std::shared_ptr<Connection> connection);
            
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
            
            virtual void addConnection(std::shared_ptr<Connection> connection) = 0;
            virtual void removeConnection(std::shared_ptr<Connection> connection) = 0;
            virtual void send(std::shared_ptr<Connection> connection, std::shared_ptr<void> data) = 0;
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
            
            bool start();
            
            void addConnection(std::shared_ptr<Connection> connection);
            void removeConnection(std::shared_ptr<Connection> connection);
            void send(std::shared_ptr<Connection> connection, std::shared_ptr<void> data);
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
            
            bool start();
            
            void addConnection(std::shared_ptr<Connection> connection);
            void removeConnection(std::shared_ptr<Connection> connection);
            void send(std::shared_ptr<Connection> connection, std::shared_ptr<void> data);
        private:
            QueueContext _queueContext;
        };
        
    public:
        // 注意：sendThreadNum和receiveThreadNum不能同为0，因为同一线程中一个fd不能同时在两个协程中进行处理，会触发EEXIST错误
        IO(uint16_t receiveThreadNum, uint16_t sendThreadNum);
        
        bool start();
        
        void destroy(); // 销毁IO
        
        Receiver *getReceiver() { return _receiver; }
        Sender *getSender() { return _sender; }
        
    private:
        ~IO();  // 不允许在栈上创建IO
        
    private:
        uint16_t _receiveThreadNum;
        uint16_t _sendThreadNum;
        
        Receiver *_receiver;
        Sender *_sender;
    };
    
}

#endif /* corpc_io_h */
