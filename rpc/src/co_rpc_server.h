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
#include "co_rpc_inner.h"

#include <vector>
#include <map>
#include <thread>
#include <google/protobuf/service.h>
#include <stdio.h>

namespace CoRpc {

    class Server {
        
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
        
        class Receiver {
        protected:
            struct ConnectedContext {
                Receiver *receiver;
                int fd;
            };
            
            struct QueueContext {
                Receiver *_receiver;
                
                // 消息队列
                ServerConnectFdQueue _queue;
            };
            
        public:
            Receiver(Server *server):_server(server) {}
            virtual ~Receiver() = 0;
            
            virtual void initForAcceptor() = 0;
            
            virtual bool start() = 0;
            
            virtual void postConnection(int fd) = 0;
            
        protected:
            static void *connectDispatchRoutine( void * arg );
            
            static void *connectHandleRoutine( void * arg );
            
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
            
            void postConnection(int fd);
            
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
            
            void postConnection(int fd);
            
        private:
            QueueContext _queueContext;
        };
        
        class Worker {
        protected:
            struct QueueContext {
                Worker *_worker;
                
                // 消息队列
                ServerWorkerTaskQueue _queue;
            };
            
        public:
            Worker(Server *server): _server(server) {}
            virtual ~Worker() = 0;
            
            virtual bool start() = 0;
            
            virtual void postRpcTask(ServerWorkerTask *task) = 0;
            
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
            
            void postRpcTask(ServerWorkerTask *task);
            
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
            
            void postRpcTask(ServerWorkerTask *task);
            
        private:
            QueueContext _queueContext;
        };
        
    public:
        Server(bool acceptInNewThread, uint16_t receiveThreadNum, uint16_t workThreadNum, const std::string& ip, uint16_t port);
        
        bool registerService(::google::protobuf::Service *rpcService);
        
        google::protobuf::Service *getService(uint32_t serviceId) const;
        
        const MethodData *getMethod(uint32_t serviceId, uint32_t methodId) const;
        
        bool start();
        
        void destroy(); // 销毁Server
        
        Acceptor *getAcceptor() { return _acceptor; }
        Receiver *getReceiver() { return _receiver; }
        Worker *getWorker() { return _worker; }
        
        const std::string &getIP() { return _ip; }
        uint16_t getPort() { return _port; }
        
    private:
        ~Server();  // 不允许在栈上创建server
        
    private:
        std::map<uint32_t, ServiceData> _services;
        
        bool _acceptInNewThread;
        uint16_t _receiveThreadNum;
        uint16_t _workThreadNum;
        std::string _ip;
        uint16_t _port;
        
        Acceptor *_acceptor;
        Receiver *_receiver;
        Worker *_worker;
    };
    
}

#endif /* co_rpc_server_h */
