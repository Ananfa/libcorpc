//
//  co_rpc_server.h
//  rpcsvr
//
//  Created by Xianke Liu on 2017/11/21.
//  Copyright © 2017年 Dena. All rights reserved.
//

#ifndef co_rpc_server_h
#define co_rpc_server_h

#include "co_routine.h"
#include "corpc_inner.h"

#include <vector>
#include <list>
#include <map>
#include <string>
#include <thread>
#include <google/protobuf/service.h>
#include <stdio.h>

#define CORPC_MAX_BUFFER_SIZE 0x100000

namespace CoRpc {

    class Server {
        
        struct RpcTask {
            google::protobuf::Service *service;
            const google::protobuf::MethodDescriptor *method_descriptor;
            const google::protobuf::Message* request;
            google::protobuf::Message* response;
            google::protobuf::RpcController *controller;
            uint64_t callId;
        };
        
        class Connection;
        struct SenderTask {
            enum TaskType {INIT, CLOSE, DATA};
            std::shared_ptr<Connection> connection;
            TaskType type;
            RpcTask *rpcTask;
        };
        
        struct ReceiverTask {
            std::shared_ptr<Connection> connection;
        };
        
        struct MethodData {
            const google::protobuf::MethodDescriptor *_method_descriptor;
            const google::protobuf::Message *_request_proto;
            const google::protobuf::Message *_response_proto;
        };
        
        struct ServiceData {
            google::protobuf::Service *rpcService;
            std::vector<MethodData> methods;
        };
        
#ifdef USE_NO_LOCK_QUEUE
        typedef Co_MPSC_NoLockQueue<ReceiverTask*, static_cast<struct ReceiverTask *>(NULL)> ReceiverTaskQueue; // 用于从Acceptor向Receiver传递建立的连接fd
        
        struct WorkerTask {
            std::shared_ptr<Connection> connection;
            RpcTask *rpcTask;
        };
        
        typedef Co_MPSC_NoLockQueue<WorkerTask*, static_cast<struct WorkerTask *>(NULL)> WorkerTaskQueue; // 用于从rpc收发协程向worker发送rpc任务（注意：用于pipe通知版本）
        //typedef MPSC_NoLockQueue<WorkerTask*, static_cast<struct WorkerTask *>(NULL)> WorkerTaskQueue; // 用于从rpc收发协程向worker发送rpc任务（注意：用于轮询版本）
        
        typedef Co_MPSC_NoLockQueue<SenderTask*, static_cast<struct SenderTask *>(NULL)> SenderTaskQueue; // 用于向sender发送任务
#else
        typedef CoSyncQueue<ReceiverTask*, static_cast<struct ReceiverTask *>(NULL)> ReceiverTaskQueue; // 用于从Acceptor向Receiver传递建立的连接fd
        
        struct WorkerTask {
            std::shared_ptr<Connection> connection;
            RpcTask *rpcTask;
        };
        
        typedef CoSyncQueue<WorkerTask*, static_cast<struct WorkerTask *>(NULL)> WorkerTaskQueue; // 用于从rpc收发协程向worker发送rpc任务（注意：用于pipe通知版本）
        //typedef SyncQueue<WorkerTask*, static_cast<struct WorkerTask *>(NULL)> WorkerTaskQueue; // 用于从rpc收发协程向worker发送rpc任务（注意：用于轮询版本）
        
        typedef CoSyncQueue<SenderTask*, static_cast<struct SenderTask *>(NULL)> SenderTaskQueue; // 用于向sender发送任务
#endif
        
        class Receiver;
        class Sender;
        class Connection {
        public:
            Connection(int fd, Receiver* receiver, Sender* sender);
            ~Connection();
            
        public:
            int getfd() { return _fd; }
            
            int getSendThreadIndex() { return _sendThreadIndex; }
            void setSendThreadIndex(int threadIndex) { _sendThreadIndex = threadIndex; }
            int getRecvThreadIndex() { return _recvThreadIndex; }
            void setRecvThreadIndex(int threadIndex) { _recvThreadIndex = threadIndex; }
            
            Sender *getSender() { return _sender; }
            Receiver *getReceiver() { return _receiver; }
        private:
            Receiver *_receiver;
            Sender *_sender;
            int _fd; // connect fd
            bool _routineHang; // deamon协程是否挂起
            stCoRoutine_t* _routine; // deamon协程
            
            int _sendThreadIndex; // 分配到sender的线程下标
            int _recvThreadIndex; // 分配到receiver的线程下标
            
            std::list<RpcTask*> _respList; // 等待发送的response数据
            
            // buff相关
            std::string _buffs;
            uint8_t *_buf;
            uint32_t _startIndex;
            uint32_t _endIndex;
            
            std::atomic<bool> _isClosing; // 是否正在关闭
            std::atomic<bool> _canClose; // 是否可调用close（当sender中fd相关协程退出时设置canClose为true，receiver中fd相关协程才可以进行close调用）
            
        public:
            friend class Receiver;
            friend class Sender;
        };
        
        class Acceptor {
        public:
            Acceptor(Server *server): _server(server), _listen_fd(-1) {}
            virtual ~Acceptor() = 0;
            
            virtual bool start() = 0;
            
        protected:
            bool init();
            
            static void *acceptRoutine( void * arg );
            
        protected:
            Server *_server;
            
        private:
            int _listen_fd;
        };
        
        class ThreadAcceptor: public Acceptor {
        public:
            ThreadAcceptor(Server *server): Acceptor(server) {}
            virtual ~ThreadAcceptor() {}
            
            bool start();
            
        protected:
            static void threadEntry( ThreadAcceptor *self );
            
        private:
            
            std::thread _t; // 保持的accept线程
        };
        
        class CoroutineAcceptor: public Acceptor {
        public:
            CoroutineAcceptor(Server *server): Acceptor(server) {}
            ~CoroutineAcceptor() {}
            
            bool start();
        };
        
        // Receiver负责rpc连接的数据接受
        class Receiver {
        protected:
            struct QueueContext {
                Receiver *_receiver;
                
                // 消息队列
                ReceiverTaskQueue _queue;
            };
            
        public:
            Receiver(Server *server):_server(server) {}
            virtual ~Receiver() = 0;
            
            virtual void initForAcceptor() = 0;
            
            virtual bool start() = 0;
            
            virtual void addConnection(std::shared_ptr<Connection> connection) = 0;
            
        protected:
            static void *connectionDispatchRoutine( void * arg );
            
            static void *connectionRoutine( void * arg );
            
        protected:
            Server *_server;
        };
        
        class MultiThreadReceiver: public Receiver {
            // 线程相关数据
            struct ThreadData {
                QueueContext _queueContext;
                
                // 保持thread对象
                std::thread _t;
            };
            
        public:
            MultiThreadReceiver(Server *server, uint16_t threadNum): Receiver(server), _threadNum(threadNum), _threadDatas(threadNum) {}
            virtual ~MultiThreadReceiver() {}
            
            void initForAcceptor();
            
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
            CoroutineReceiver(Server *server): Receiver(server) { _queueContext._receiver = this; }
            virtual ~CoroutineReceiver() {}
            
            void initForAcceptor();
            
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
            Sender(Server *server):_server(server) {}
            virtual ~Sender() = 0;
            
            virtual bool start() = 0;
            
            virtual void addConnection(std::shared_ptr<Connection> connection) = 0;
            virtual void removeConnection(std::shared_ptr<Connection> connection) = 0;
            virtual void postRpcTask(std::shared_ptr<Connection> connection, RpcTask *rpcTask) = 0;
        protected:
            static void *taskQueueRoutine( void * arg );
            static void *connectionRoutine( void * arg );
            
        private:
            Server *_server;
        };
        
        class MultiThreadSender: public Sender {
            // 线程相关数据
            struct ThreadData {
                QueueContext _queueContext;
                
                // 保持thread对象
                std::thread _t;
            };
            
        public:
            MultiThreadSender(Server *server, uint16_t threadNum): Sender(server), _threadNum(threadNum), _threadDatas(threadNum) {}
            virtual ~MultiThreadSender() {}
            
            bool start();
            
            void addConnection(std::shared_ptr<Connection> connection);
            void removeConnection(std::shared_ptr<Connection> connection);
            void postRpcTask(std::shared_ptr<Connection> connection, RpcTask *rpcTask);
        private:
            static void threadEntry( ThreadData *tdata );
            
        private:
            uint16_t _threadNum;
            uint16_t _lastThreadIndex;
            std::vector<ThreadData> _threadDatas;
        };
        
        class CoroutineSender: public Sender {
        public:
            CoroutineSender(Server *server): Sender(server) { _queueContext._sender = this; }
            virtual ~CoroutineSender() {}
            
            bool start();
            
            void addConnection(std::shared_ptr<Connection> connection);
            void removeConnection(std::shared_ptr<Connection> connection);
            void postRpcTask(std::shared_ptr<Connection> connection, RpcTask *rpcTask);
        private:
            QueueContext _queueContext;
        };
        
        class Worker {
        protected:
            struct QueueContext {
                Worker *_worker;
                
                // 消息队列
                WorkerTaskQueue _queue;
            };
            
        public:
            Worker(Server *server): _server(server) {}
            virtual ~Worker() = 0;
            
            virtual bool start() = 0;
            
            virtual void postRpcTask(WorkerTask *task) = 0;
            
        protected:
            static void *taskHandleRoutine( void * arg );
            
            static void *taskCallRoutine( void * arg );
            
        protected:
            Server *_server;
        };
        
        class MultiThreadWorker: public Worker {
            // 线程相关数据
            struct ThreadData {
                // 消息队列
                QueueContext _queueContext;
                
                // thread对象
                std::thread _t;
            };
            
        public:
            MultiThreadWorker(Server *server, uint16_t threadNum): Worker(server), _threadNum(threadNum), _threadDatas(threadNum) {}
            virtual ~MultiThreadWorker() {}
            
            bool start();
            
            void postRpcTask(WorkerTask *task);
            
        protected:
            static void threadEntry( ThreadData *tdata );
            
        private:
            uint16_t _threadNum;
            uint16_t _lastThreadIndex;
            std::vector<ThreadData> _threadDatas;
        };
        
        class CoroutineWorker: public Worker {
        public:
            CoroutineWorker(Server *server): Worker(server) { _queueContext._worker = this; }
            virtual ~CoroutineWorker() {}
            
            bool start();
            
            void postRpcTask(WorkerTask *task);
            
        private:
            QueueContext _queueContext;
        };
        
    public:
        // 注意：sendThreadNum和receiveThreadNum不能同为0，因为同一线程中一个fd不能同时在两个协程中进行处理，会触发EEXIST错误
        Server(bool acceptInNewThread, uint16_t receiveThreadNum, uint16_t sendThreadNum, uint16_t workThreadNum, const std::string& ip, uint16_t port);
        
        bool registerService(::google::protobuf::Service *rpcService);
        
        google::protobuf::Service *getService(uint32_t serviceId) const;
        
        const MethodData *getMethod(uint32_t serviceId, uint32_t methodId) const;
        
        bool start();
        
        void destroy(); // 销毁Server
        
        Acceptor *getAcceptor() { return _acceptor; }
        Receiver *getReceiver() { return _receiver; }
        Sender *getSender() { return _sender; }
        Worker *getWorker() { return _worker; }
        
        const std::string &getIP() { return _ip; }
        uint16_t getPort() { return _port; }
        
    private:
        ~Server();  // 不允许在栈上创建server
        
    private:
        std::map<uint32_t, ServiceData> _services;
        
        bool _acceptInNewThread;
        uint16_t _receiveThreadNum;
        uint16_t _sendThreadNum;
        uint16_t _workThreadNum;
        std::string _ip;
        uint16_t _port;
        
        Acceptor *_acceptor;
        Receiver *_receiver;
        Sender *_sender;
        Worker *_worker;
    };
    
}

#endif /* co_rpc_server_h */
