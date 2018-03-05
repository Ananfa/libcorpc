//
//  corpc_inner_server.h
//  rpccli
//
//  Created by Xianke Liu on 2018/3/1.
//Copyright © 2018年 Dena. All rights reserved.
//

#ifndef corpc_inner_h
#define corpc_inner_h

#include "co_routine.h"
#include "corpc_inner.h"

#include <map>
#include <google/protobuf/service.h>

namespace CoRpc {
    
    // 进程内线程间通信rpc实现
    namespace Inner {
        
        class Client;
        class Server;
        
        struct Request {
            Client *client; // 发起调用的Client，用于在rpc处理完成后返回结果给发起者
            Server *server;
            stCoRoutine_t *co;
            const google::protobuf::Message* request;
            google::protobuf::Message* response;
            google::protobuf::RpcController *controller;
            uint32_t serviceId;
            uint32_t methodId;
        };
        
#ifdef USE_NO_LOCK_QUEUE
        typedef Co_MPSC_NoLockQueue<Request*, static_cast<struct Request *>(NULL)> RequestQueue;
        typedef Co_MPSC_NoLockQueue<stCoRoutine_t*, static_cast<struct stCoRoutine_t *>(NULL)> ResponseQueue;
#else
        typedef CoSyncQueue<Request*, static_cast<struct Request *>(NULL)> RequestQueue;
        typedef CoSyncQueue<stCoRoutine_t*, static_cast<struct stCoRoutine_t *>(NULL)> ResponseQueue;
#endif
        
        class Client {
            class Channel : public google::protobuf::RpcChannel {
            public:
                Channel(Client *client, Server *server): _client(client), _server(server) {}
                virtual void CallMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller, const google::protobuf::Message *request, google::protobuf::Message *response, google::protobuf::Closure *done);
                
            private:
                virtual ~Channel() {}
                
            private:
                Client *_client;
                Server *_server;
                
            public:
                friend class Client;
            };
            
        public:
            Client() {}
            
        private:
            ~Client() {}
            
            void response(stCoRoutine_t *co);
            
            static void *responseQueueRoutine( void * arg );
            
        private:
            ResponseQueue _queue;
            
        public:
            friend class Server;
            
        };
        
        class Server {
            
        public:
            Server() {}
            
            bool registerService(::google::protobuf::Service *rpcService);
            
            google::protobuf::Service *getService(uint32_t serviceId) const;
            
            const MethodData *getMethod(uint32_t serviceId, uint32_t methodId) const;
            
            void start();
            
            void destroy() { delete this; } // 销毁Server
            
        private:
            ~Server() {}  // 不允许在栈上创建server
            
            void request(Request *request);
            
            static void *requestQueueRoutine( void * arg );   // 处理Request，若rpc定义了need_coroutine，启动单独的taskCallRoutine协程来处理rpc任务
            
            static void *requestRoutine( void * arg );
        private:
            std::map<uint32_t, ServiceData> _services;
            
            RequestQueue _queue;
            
        public:
            friend class Client::Channel;
        };
        
    }
    
}
#endif /* corpc_inner_h */
