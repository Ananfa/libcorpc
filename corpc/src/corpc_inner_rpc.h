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
#include "corpc_queue.h"

#include <thread>
#include <map>
#include <vector>
#include <google/protobuf/service.h>
#include <google/protobuf/message.h>

namespace corpc {
    
    // 进程内线程间通信rpc实现
    class InnerRpcServer;
    
    struct InnerRpcRequest {
        InnerRpcServer *server;
        std::shared_ptr<RpcClientTask> rpcTask;
    };
    
    typedef MPMC_NoLockBlockQueue<InnerRpcRequest*> InnerRpcRequestQueue;
    
    class InnerRpcChannel : public google::protobuf::RpcChannel {
    public:
        InnerRpcChannel(InnerRpcServer *server): server_(server) {}
        virtual void CallMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller, const google::protobuf::Message *request, google::protobuf::Message *response, google::protobuf::Closure *done);
        
    private:
        virtual ~InnerRpcChannel() {}
        
    private:
        InnerRpcServer *server_;
    };
    
    class InnerRpcServer {
    public:
        InnerRpcServer() {}
        
        void start(uint32_t workerThreadNum = 0);
        
        bool registerService(::google::protobuf::Service *rpcService);
        
        google::protobuf::Service *getService(uint32_t serviceId) const;
        
        const MethodData *getMethod(uint32_t serviceId, uint32_t methodId) const;
        
        void destroy() { delete this; } // 销毁Server
        
    private:
        ~InnerRpcServer() {}  // 不允许在栈上创建server
        
        static void threadEntry( InnerRpcServer * self );
        
        static void *requestQueueRoutine( void * arg );   // 处理Request，若rpc定义了need_coroutine，启动单独的taskCallRoutine协程来处理rpc任务
        
        static void *requestRoutine( void * arg );
    private:
        std::map<uint32_t, ServiceData> services_;
        
        InnerRpcRequestQueue queue_;
        
        std::vector<std::thread> ts_;
    public:
        friend class InnerRpcChannel;
    };
    
}
#endif /* corpc_inner_rpc_h */
