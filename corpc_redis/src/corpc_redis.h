/*
 * Created by Xianke Liu on 2018/5/16.
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

#ifndef corpc_redis_h
#define corpc_redis_h

#include "corpc_routine_env.h"
#include "corpc_controller.h"
#include "corpc_inner_rpc.h"

#include "corpc_thirdparty.pb.h"

#include <hiredis.h>

namespace corpc {
    
    class RedisConnectPool : public thirdparty::ThirdPartyService {
    public:
        class Proxy {
        public:
            redisContext* take();
            void put(redisContext* redis, bool error);
            
        private:
            Proxy(RedisConnectPool *pool);
            ~Proxy();
            
            static void callDoneHandle(::google::protobuf::Message *request, corpc::Controller *controller);
            
        private:
            thirdparty::ThirdPartyService::Stub *_stub;
            
        public:
            friend class RedisConnectPool;
        };
        
    public:
        virtual void take(::google::protobuf::RpcController* controller,
                          const Void* request,
                          thirdparty::TakeResponse* response,
                          ::google::protobuf::Closure* done);
        virtual void put(::google::protobuf::RpcController* controller,
                         const thirdparty::PutRequest* request,
                         Void* response,
                         ::google::protobuf::Closure* done);
        
    public:
        static RedisConnectPool* create(const char *host, unsigned int port, uint32_t maxConnectNum, uint32_t maxIdleNum);
        
        Proxy* getProxy();
        
    private:
        RedisConnectPool(const char *host, unsigned int port, uint32_t maxConnectNum, uint32_t maxIdleNum);
        ~RedisConnectPool() {}
        
        void init();
        
    private:
        std::string _host;
        unsigned int _port;
        
        uint32_t _maxConnectNum;    // 与mysql数据库最多建立的连接数
        uint32_t _maxIdleNum;       // 最大空闲连接数量
        uint32_t _realConnectCount; // 当前实际建立连接的数量
        
        std::list<redisContext*> _idleList; // 空闲连接表
        std::list<stCoRoutine_t*> _waitingList; // 等待队列：当连接数量达到最大时，新的请求需要等待
        
        InnerRpcServer *_server;
        std::map<pid_t, Proxy*> _threadProxyMap; // 线程相关代理
    };

}

#endif /* corpc_redis_h */
