/*
 * Created by Xianke Liu on 2017/11/1.
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

#ifndef corpc_rpc_client_h
#define corpc_rpc_client_h

#include "corpc_io.h"

#include <list>
#include <map>
#include <queue>
#include <mutex>
#include <unistd.h>

#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>

// rpc channel需要注册到client中才能被服务
namespace corpc {
    
    // 注意：RpcClient的实现不考虑运行时的关闭销毁，只能通过关闭程序来关闭
    class RpcClient {
        
        struct RpcTask {
            pid_t pid;
            stCoRoutine_t *co;
            const google::protobuf::Message* request;
            google::protobuf::Message* response;
            google::protobuf::RpcController *controller;
            google::protobuf::Closure *done;
            uint32_t serviceId;
            uint32_t methodId;
        };
        
        struct ClientTask {
            google::protobuf::RpcChannel *channel;
            std::shared_ptr<RpcTask> rpcTask;
        };
        
    public:
        class Channel;
    private:
        class Connection: public corpc::Connection {
            enum Status {CLOSED, CONNECTING, CONNECTED};
            typedef std::list<std::shared_ptr<ClientTask> > WaitTaskList;
            typedef std::map<uint64_t, std::shared_ptr<ClientTask> > WaitTaskMap;
            
        public:
            Connection(Channel *channel);
            ~Connection() {}
            
            virtual void onClose();
            virtual void cleanDataOnClosing(std::shared_ptr<void>& data);
            
        private:
            Channel *_channel;
            
            Status _st;
            
            WaitTaskList _waitSendTaskCoList;// 等待发送RPC请求的任务
            WaitTaskMap _waitResultCoMap; // 等待接受RPC结果的任务
            std::mutex _waitResultCoMapMutex; // _waitResultCoMap需要进行线程同步
            
        public:
            friend class Channel;
            friend class RpcClient;
            friend class Decoder;
            friend class Encoder;
        };
        
    public:
        class Channel : public google::protobuf::RpcChannel {
        public:
            Channel(RpcClient *client, const std::string& ip, uint32_t port, uint32_t connectNum = 1);
            virtual void CallMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller, const google::protobuf::Message *request, google::protobuf::Message *response, google::protobuf::Closure *done);
            
        private:
            virtual ~Channel();
            
            std::shared_ptr<Connection>& getNextConnection();
            
        private:
            std::string _ip;
            uint32_t _port;
            
            RpcClient *_client;
            std::vector<std::shared_ptr<Connection>> _connections;
            uint32_t _conIndex;
            
            bool _connectDelay; // 是否需要延迟连接
            
        public:
            friend class ClientConnection;
            friend class RpcClient;
            friend class Encoder;
        };
        
        typedef std::map<Channel*, Channel*> ChannelSet;
        
        struct ConnectionTask {
            enum TaskType {CLOSE, CONNECT};
            TaskType type;
            std::shared_ptr<Connection> connection;
        };
        
#ifdef USE_NO_LOCK_QUEUE
        typedef Co_MPSC_NoLockQueue<ConnectionTask*> ConnectionTaskQueue;
        typedef Co_MPSC_NoLockQueue<std::shared_ptr<ClientTask> > ClientTaskQueue;
#else
        typedef CoSyncQueue<ConnectionTask*> ConnectionTaskQueue;
        typedef CoSyncQueue<std::shared_ptr<ClientTask> > ClientTaskQueue;
#endif
       
    public:
        static RpcClient* create(IO *io);
        
        bool registerChannel(Channel *channel);
        
    private:
        RpcClient(IO *io);
        
        ~RpcClient() {}
        
        static void* decode(std::shared_ptr<corpc::Connection> &connection, uint8_t *head, uint8_t *body, int size);
        
        static bool encode(std::shared_ptr<corpc::Connection> &connection, std::shared_ptr<void>& data, uint8_t *buf, int space, int &size, std::string &downflowBuf, uint32_t &downflowBufSentNum);
        
        // 注意：需要开启线程来执行connectionRoutine和taskHandleRoutine协程，原因是not_care_response类型的rpc调用不会触发调用处协程切换，
        // 此时如果在同一线程中开启connectionRoutine和taskHandleRoutine协程，它们将等到调用处协程将来让出执行后才能得到调度，导致数据不能及时发送。
        static void threadEntry(RpcClient *self);
        
        static void *connectionRoutine(void * arg);  // 负责为connection连接建立和断线处理
        
        static void *taskHandleRoutine(void * arg);   // 负责将rpc调用请求通过connection交由sender发出
        
        virtual void start();
        
    private:
        IO *_io;
        
        std::thread _t; // 任务处理线程
        
        ConnectionTaskQueue _connectionTaskQueue; // connectionRoutine处理的连接任务队列
        
        ClientTaskQueue _taskQueue; // taskHandleRoutine
        
        ChannelSet _channelSet; // 注册的channel
        
        PipelineFactory *_pipelineFactory;
    };
}

#endif /* corpc_rpc_client_h */
