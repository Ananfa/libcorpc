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
#include "corpc_rpc_common.h"

#include <vector>
#include <list>
#include <map>
#include <string>
#include <thread>
#include <google/protobuf/service.h>

namespace corpc {

    class RpcServer: public corpc::Server {
        
        class MultiThreadWorker: public corpc::MultiThreadWorker {
        public:
            MultiThreadWorker(RpcServer *server, uint16_t threadNum): corpc::MultiThreadWorker(threadNum), _server(server) {}
            virtual ~MultiThreadWorker() {}
            
        protected:
            static void *taskCallRoutine( void * arg );
            
            virtual void handleMessage(void *msg); // 注意：处理完消息需要自己删除msg
            
        private:
            RpcServer *_server;
        };
        
        class CoroutineWorker: public corpc::CoroutineWorker {
        public:
            CoroutineWorker(RpcServer *server): _server(server) {}
            virtual ~CoroutineWorker() {}
            
        protected:
            static void *taskCallRoutine( void * arg );
            
            virtual void handleMessage(void *msg); // 注意：处理完消息需要自己删除msg
            
        private:
            RpcServer *_server;
        };
        
        class Connection: public corpc::Connection {
        public:
            Connection(int fd, RpcServer* server);
            virtual ~Connection();
            
            virtual void onClose();
            
            RpcServer *getServer() { return _server; }
        private:
            RpcServer *_server;
        };

        struct WorkerTask {
            std::shared_ptr<corpc::Connection> connection;
            std::shared_ptr<RpcServerTask> rpcTask;
        };
        
    public:
        static RpcServer* create(IO *io, uint16_t workThreadNum, const std::string& ip, uint16_t port);
        
        bool registerService(::google::protobuf::Service *rpcService);
        
        google::protobuf::Service *getService(uint32_t serviceId) const;
        
        const MethodData *getMethod(uint32_t serviceId, uint32_t methodId) const;
        
        // override
        virtual corpc::Connection * buildConnection(int fd);
        
        // override
        virtual void onConnect(std::shared_ptr<corpc::Connection>& connection) {}
        
        // override
        virtual void onClose(std::shared_ptr<corpc::Connection>& connection);
        
    private:
        RpcServer(IO *io, uint16_t workThreadNum, const std::string& ip, uint16_t port);
        virtual ~RpcServer();  // 不允许在栈上创建server
        
        static void* decode(std::shared_ptr<corpc::Connection> &connection, uint8_t *head, uint8_t *body, int size);
        
        static bool encode(std::shared_ptr<corpc::Connection> &connection, std::shared_ptr<void>& data, uint8_t *buf, int space, int &size, std::string &downflowBuf, uint32_t &downflowBufSentNum);
        
    private:
        std::map<uint32_t, ServiceData> _services;
    };
    
}

#endif /* corpc_rpc_server_h */
