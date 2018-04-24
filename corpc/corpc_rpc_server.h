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
        
        class Connection: public CoRpc::Connection {
        public:
            Connection(int fd, RpcServer* server);
            virtual ~Connection();
            
            virtual void onClose();
            
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
        
    public:
        static RpcServer* create(IO *io, uint16_t workThreadNum, const std::string& ip, uint16_t port);
        
        bool registerService(::google::protobuf::Service *rpcService);
        
        google::protobuf::Service *getService(uint32_t serviceId) const;
        
        const MethodData *getMethod(uint32_t serviceId, uint32_t methodId) const;
        
        // override
        virtual CoRpc::PipelineFactory * getPipelineFactory();
        
        // override
        virtual CoRpc::Connection * buildConnection(int fd);
        
        // override
        virtual void onConnect(std::shared_ptr<CoRpc::Connection>& connection) {}
        
        // override
        virtual void onClose(std::shared_ptr<CoRpc::Connection>& connection);
        
    private:
        RpcServer(IO *io, uint16_t workThreadNum, const std::string& ip, uint16_t port);
        virtual ~RpcServer();  // 不允许在栈上创建server
        
        static void* decode(std::shared_ptr<CoRpc::Connection> &connection, uint8_t *head, uint8_t *body, int size);
        
        static bool encode(std::shared_ptr<CoRpc::Connection> &connection, std::shared_ptr<void>& data, uint8_t *buf, int space, int &size);
        
        bool start();
        
    private:
        std::map<uint32_t, ServiceData> _services;
        
        Acceptor *_acceptor;
        Worker *_worker;
        
        PipelineFactory *_pipelineFactory;
    };
    
}

#endif /* corpc_rpc_server_h */
