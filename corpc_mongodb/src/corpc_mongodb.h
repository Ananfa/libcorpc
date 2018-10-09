/*
 * Created by Xianke Liu on 2018/5/15.
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

#ifndef corpc_mongodb_h
#define corpc_mongodb_h

#include "corpc_routine_env.h"
#include "corpc_controller.h"
#include "corpc_inner_rpc.h"

#include "corpc_thirdparty.pb.h"

#include <mongoc.h>

namespace corpc {
    
    class MongodbConnectPool : public thirdparty::ThirdPartyService {
        struct IdleHandle {
            mongoc_client_t *handle;
            time_t time;  // 开始idle的时间
        };
        
    public:
        class Proxy {
        public:
            mongoc_client_t* take();
            void put(mongoc_client_t* mongoc, bool error);
            
        private:
            Proxy(): _stub(nullptr) {}
            ~Proxy();
            
            void init(InnerRpcServer *server);
            
            static void callDoneHandle(::google::protobuf::Message *request, corpc::Controller *controller);
            
        private:
            thirdparty::ThirdPartyService::Stub *_stub;
            
        public:
            friend class MongodbConnectPool;
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
        static MongodbConnectPool* create(const char *uri, uint32_t maxConnectNum);
        
    private:
        MongodbConnectPool(const char *uri, uint32_t maxConnectNum);
        ~MongodbConnectPool() {}
        
        void init();
        
        static void *clearIdleRoutine( void *arg );
        
    private:
        static std::mutex _initMutex;
        static bool _initialized;
        
    public:
        Proxy proxy;
        
    private:
        std::string _uri;
        
        uint32_t _maxConnectNum;    // 与mysql数据库最多建立的连接数
        uint32_t _realConnectCount; // 当前实际建立连接的数量
        
        std::list<IdleHandle> _idleList; // 空闲连接表
        std::list<stCoRoutine_t*> _waitingList; // 等待队列：当连接数量达到最大时，新的请求需要等待
        
        InnerRpcServer *_server;
    };

}

#endif /* corpc_mongodb_h */
