/*
 * Created by Xianke Liu on 2018/3/1.
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

#ifndef corpc_inner_rpc_h
#define corpc_inner_rpc_h

#include "co_routine.h"

#include <map>
#include <google/protobuf/service.h>

namespace corpc {
    
    // 进程内线程间通信rpc实现
    class InnerRpcClient;
    class InnerRpcServer;
    
    struct InnerRpcRequest {
        InnerRpcClient *client; // 发起调用的Client，用于在rpc处理完成后返回结果给发起者
        InnerRpcServer *server;
        pid_t pid;
        stCoRoutine_t *co;
        const google::protobuf::Message* request;
        google::protobuf::Message* response;
        google::protobuf::RpcController *controller;
        google::protobuf::Closure *done;
        uint32_t serviceId;
        uint32_t methodId;
    };
    
#ifdef USE_NO_LOCK_QUEUE
    typedef Co_MPSC_NoLockQueue<InnerRpcRequest*> InnerRpcRequestQueue;
    //typedef Co_MPSC_NoLockQueue<stCoRoutine_t*> InnerRpcResponseQueue;
#else
    typedef CoSyncQueue<InnerRpcRequest*> InnerRpcRequestQueue;
    //typedef CoSyncQueue<stCoRoutine_t*> InnerRpcResponseQueue;
#endif
    
    class InnerRpcClient {
    public:
        class Channel : public google::protobuf::RpcChannel {
        public:
            Channel(InnerRpcClient *client, InnerRpcServer *server): _client(client), _server(server) {}
            virtual void CallMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller, const google::protobuf::Message *request, google::protobuf::Message *response, google::protobuf::Closure *done);
            
        private:
            virtual ~Channel() {}
            
        private:
            InnerRpcClient *_client;
            InnerRpcServer *_server;
            
        public:
            friend class InnerRpcClient;
        };
        
    public:
        static InnerRpcClient* instance();
        
    private:
        InnerRpcClient() {}
        ~InnerRpcClient() {}
        
        //void start();
        
        //static void *responseQueueRoutine( void * arg );
        
    private:
        //InnerRpcResponseQueue _queue;
        
        static __thread InnerRpcClient *_instance;
        
    public:
        friend class InnerRpcServer;
        
    };
    
    class InnerRpcServer {
        
    public:
        static InnerRpcServer* create();
        
        bool registerService(::google::protobuf::Service *rpcService);
        
        google::protobuf::Service *getService(uint32_t serviceId) const;
        
        const MethodData *getMethod(uint32_t serviceId, uint32_t methodId) const;
        
        void destroy() { delete this; } // 销毁Server
        
    private:
        InnerRpcServer() {}
        ~InnerRpcServer() {}  // 不允许在栈上创建server
        
        void start();
        
        static void *requestQueueRoutine( void * arg );   // 处理Request，若rpc定义了need_coroutine，启动单独的taskCallRoutine协程来处理rpc任务
        
        static void *requestRoutine( void * arg );
    private:
        std::map<uint32_t, ServiceData> _services;
        
        InnerRpcRequestQueue _queue;
        
    public:
        friend class InnerRpcClient::Channel;
    };
    
}
#endif /* corpc_inner_rpc_h */
