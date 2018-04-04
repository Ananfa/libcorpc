/*
 * Created by Xianke Liu on 2017/11/21.
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

#ifndef corpc_server_h
#define corpc_server_h

#include "corpc_io.h"

#include <vector>
#include <list>
#include <map>
#include <string>
#include <thread>
#include <google/protobuf/service.h>

namespace CoRpc {

    class Server {
        class Decoder: public CoRpc::Decoder {
        public:
            Decoder() {}
            virtual ~Decoder();
            
            virtual bool decode(std::shared_ptr<CoRpc::Connection> &connection, uint8_t *head, uint8_t *body, int size);
            
        };
        
        class Router: public CoRpc::Router {
        public:
            Router() {}
            virtual ~Router();
            
            virtual bool route(std::shared_ptr<CoRpc::Connection> &connection, int type, void *msg);
        };
        
        class Encoder: public CoRpc::Encoder {
        public:
            Encoder() {}
            virtual ~Encoder();
            
            virtual bool encode(std::shared_ptr<CoRpc::Connection> &connection, std::shared_ptr<void> &data, uint8_t *buf, int space, int &size);
        };
        
        // singleton
        class PipelineFactory: public CoRpc::PipelineFactory {
        public:
            static PipelineFactory& Instance() {
                static PipelineFactory factory;
                return factory;
            }
            
            virtual std::shared_ptr<CoRpc::Pipeline> buildPipeline(std::shared_ptr<CoRpc::Connection> &connection);
            
        private:
            PipelineFactory() {}
            PipelineFactory(PipelineFactory const&);
            PipelineFactory& operator=(PipelineFactory const&);
            ~PipelineFactory() {}
            
        private:
            std::shared_ptr<CoRpc::Decoder> _decoder;
            std::shared_ptr<CoRpc::Router> _router;
            std::shared_ptr<CoRpc::Encoder> _encoder;
        };
        
        class Connection: public CoRpc::Connection {
        public:
            Connection(int fd, Server* server);
            virtual ~Connection();
            
            virtual void onClose() {}
            
            Server *getServer() { return _server; }
        private:
            Server *_server;
            
            // 接收数据时的包头和包体
            //RpcRequestHead _reqhead;
            //char *_head_buf;
            //int _headNum;
            
            //std::string _reqdata;
            //uint8_t *_data_buf;
            //int _dataNum;
            
        };
        
        class RpcTask {
        public:
            RpcTask();
            ~RpcTask();
            
        public:
            google::protobuf::Service *service;
            const google::protobuf::MethodDescriptor *method_descriptor;
            const google::protobuf::Message* request;
            google::protobuf::Message* response;
            google::protobuf::RpcController *controller;
            uint64_t callId;
        };
        
        struct WorkerTask {
            std::shared_ptr<CoRpc::Connection> connection;
            std::shared_ptr<RpcTask> rpcTask;
        };
        
#ifdef USE_NO_LOCK_QUEUE
        typedef Co_MPSC_NoLockQueue<WorkerTask*, static_cast<struct WorkerTask *>(NULL)> WorkerTaskQueue; // 用于从rpc收发协程向worker发送rpc任务（注意：用于pipe通知版本）
        //typedef MPSC_NoLockQueue<WorkerTask*, static_cast<struct WorkerTask *>(NULL)> WorkerTaskQueue; // 用于从rpc收发协程向worker发送rpc任务（注意：用于轮询版本）
#else
        typedef CoSyncQueue<WorkerTask*, static_cast<struct WorkerTask *>(NULL)> WorkerTaskQueue; // 用于从rpc收发协程向worker发送rpc任务（注意：用于pipe通知版本）
        //typedef SyncQueue<WorkerTask*, static_cast<struct WorkerTask *>(NULL)> WorkerTaskQueue; // 用于从rpc收发协程向worker发送rpc任务（注意：用于轮询版本）
#endif
        
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
            
            virtual bool start();
            
        protected:
            static void threadEntry( ThreadAcceptor *self );
            
        private:
            
            std::thread _t; // 保持的accept线程
        };
        
        class CoroutineAcceptor: public Acceptor {
        public:
            CoroutineAcceptor(Server *server): Acceptor(server) {}
            ~CoroutineAcceptor() {}
            
            virtual bool start();
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
            static void *taskHandleRoutine( void * arg );   // 处理WorkerTask，及其中的rpc任务，若rpc定义了need_coroutine，启动单独的taskCallRoutine协程来处理rpc任务
            
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
        static Server* create(bool acceptInNewThread, uint16_t workThreadNum, const std::string& ip, uint16_t port);
        
        bool registerService(::google::protobuf::Service *rpcService);
        
        google::protobuf::Service *getService(uint32_t serviceId) const;
        
        const MethodData *getMethod(uint32_t serviceId, uint32_t methodId) const;
        
        void destroy() { delete this; } // 销毁Server
        
        const std::string &getIP() { return _ip; }
        uint16_t getPort() { return _port; }
        
    private:
        Server(IO *io, bool acceptInNewThread, uint16_t workThreadNum, const std::string& ip, uint16_t port);
        ~Server();  // 不允许在栈上创建server
        
        bool start();
        
    private:
        std::map<uint32_t, ServiceData> _services;
        
        bool _acceptInNewThread;
        uint16_t _workThreadNum;
        std::string _ip;
        uint16_t _port;
        
        IO *_io;
        Acceptor *_acceptor;
        Worker *_worker;
    };
    
}

#endif /* corpc_server_h */
