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

#ifndef corpc_rpc_server_h
#define corpc_rpc_server_h

#include "corpc_io.h"

#include <vector>
#include <list>
#include <map>
#include <string>
#include <thread>
#include <google/protobuf/service.h>

namespace CoRpc {

    class RpcServer: public CoRpc::Server {
        class Decoder: public CoRpc::Decoder {
        public:
            Decoder() {}
            virtual ~Decoder();
            
            virtual void * decode(std::shared_ptr<CoRpc::Connection> &connection, uint8_t *head, uint8_t *body, int size);
            
        };
        
        class MultiThreadWorker: public CoRpc::MultiThreadWorker {
        public:
            MultiThreadWorker(RpcServer *server, uint16_t threadNum): CoRpc::MultiThreadWorker(threadNum), _server(server) {}
            virtual ~MultiThreadWorker() {}
            
        protected:
            static void *taskCallRoutine( void * arg );
            
            virtual void handleMessage(void *msg); // 注意：处理完消息需要自己删除msg
            
        private:
            RpcServer *_server;
        };
        
        class CoroutineWorker: public CoRpc::CoroutineWorker {
        public:
            CoroutineWorker(RpcServer *server): _server(server) {}
            virtual ~CoroutineWorker() {}
            
        protected:
            static void *taskCallRoutine( void * arg );
            
            virtual void handleMessage(void *msg); // 注意：处理完消息需要自己删除msg
            
        private:
            RpcServer *_server;
        };
        
        class Encoder: public CoRpc::Encoder {
        public:
            Encoder() {}
            virtual ~Encoder();
            
            virtual bool encode(std::shared_ptr<CoRpc::Connection> &connection, std::shared_ptr<void> &data, uint8_t *buf, int space, int &size);
        };
        
        class PipelineFactory: public CoRpc::PipelineFactory {
        public:
            PipelineFactory(CoRpc::Decoder *decoder, CoRpc::Worker *worker, std::vector<CoRpc::Encoder*>&& encoders): CoRpc::PipelineFactory(decoder, worker, std::move(encoders)) {}
            ~PipelineFactory() {}
            
            virtual std::shared_ptr<CoRpc::Pipeline> buildPipeline(std::shared_ptr<CoRpc::Connection> &connection);
        };
        
        class Connection: public CoRpc::Connection {
        public:
            Connection(int fd, RpcServer* server);
            virtual ~Connection();
            
            virtual void onClose() {}
            
            RpcServer *getServer() { return _server; }
        private:
            RpcServer *_server;
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
        
        class Acceptor {
        public:
            Acceptor(RpcServer *server): _server(server), _listen_fd(-1) {}
            virtual ~Acceptor() = 0;
            
            virtual bool start() = 0;
            
        protected:
            bool init();
            
            static void *acceptRoutine( void * arg );
            
        protected:
            RpcServer *_server;
            
        private:
            int _listen_fd;
        };
        
        class ThreadAcceptor: public Acceptor {
        public:
            ThreadAcceptor(RpcServer *server): Acceptor(server) {}
            virtual ~ThreadAcceptor() {}
            
            virtual bool start();
            
        protected:
            static void threadEntry( ThreadAcceptor *self );
            
        private:
            
            std::thread _t; // 保持的accept线程
        };
        
        class CoroutineAcceptor: public Acceptor {
        public:
            CoroutineAcceptor(RpcServer *server): Acceptor(server) {}
            ~CoroutineAcceptor() {}
            
            virtual bool start();
        };
        
    public:
        static RpcServer* create(IO *io, bool acceptInNewThread, uint16_t workThreadNum, const std::string& ip, uint16_t port);
        
        bool registerService(::google::protobuf::Service *rpcService);
        
        google::protobuf::Service *getService(uint32_t serviceId) const;
        
        const MethodData *getMethod(uint32_t serviceId, uint32_t methodId) const;
        
        const std::string &getIP() { return _ip; }
        uint16_t getPort() { return _port; }
        
        // override
        virtual CoRpc::PipelineFactory * getPipelineFactory();
        
        // override
        virtual CoRpc::Connection * buildConnection(int fd);
        
    private:
        RpcServer(IO *io, bool acceptInNewThread, uint16_t workThreadNum, const std::string& ip, uint16_t port);
        virtual ~RpcServer();  // 不允许在栈上创建server
        
        bool start();
        
    private:
        std::map<uint32_t, ServiceData> _services;
        
        bool _acceptInNewThread;
        uint16_t _workThreadNum;
        std::string _ip;
        uint16_t _port;
        
        Acceptor *_acceptor;
        Worker *_worker;
        
        PipelineFactory *_pipelineFactory;
    };
    
}

#endif /* corpc_rpc_server_h */
