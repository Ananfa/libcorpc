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
#include "corpc_rpc_common.h"
#include "corpc_mutex.h"

#include <map>
#include <queue>
#include <unistd.h>

#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>

// rpc channel需要注册到client中才能被服务
namespace corpc {
    
    // 注意：RpcClient的实现不考虑运行时的关闭销毁，只能通过关闭程序来关闭
    class RpcClient {
    public:
        class Channel;
        
    private:
        class Connection;
        class ChannelCore : public std::enable_shared_from_this<ChannelCore> {
        public:
            ChannelCore(RpcClient *client, const std::string& host, uint32_t port, uint32_t connectNum);
            virtual ~ChannelCore();
            
            virtual void CallMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller, const google::protobuf::Message *request, google::protobuf::Message *response, google::protobuf::Closure *done);
            
        private:
            std::shared_ptr<Connection> getNextConnection();
            
        private:
            std::string host_;
            uint32_t port_;
            
            RpcClient *client_;
            std::vector<std::shared_ptr<Connection>> connections_;
            uint32_t conIndex_;
            
            bool connectDelay_; // 是否需要延迟连接
            
        public:
            friend class Channel;
            friend class RpcClient;
            friend class Encoder;
        };
        
        struct ClientTask {
            std::shared_ptr<ChannelCore> channel;
            std::shared_ptr<RpcClientTask> rpcTask;
        };
        
        class Connection: public corpc::Connection {
            enum Status {CLOSED, CONNECTING, CONNECTED};
            typedef std::list<std::shared_ptr<ClientTask> > WaitTaskList;
            typedef std::map<uint64_t, std::shared_ptr<ClientTask> > WaitTaskMap;
            
        public:
            Connection(std::shared_ptr<ChannelCore> channel);
            ~Connection() {}
            
            virtual void onConnect() {}
            virtual void onClose();
            virtual void cleanDataOnClosing(std::shared_ptr<void>& data);
            
        private:
            std::shared_ptr<ChannelCore> channel_;
            
            Status st_;
            
            WaitTaskList waitSendTaskCoList_;// 等待发送RPC请求的任务
            WaitTaskMap waitResultCoMap_; // 等待接受RPC结果的任务
            Mutex waitResultCoMapMutex_; // _waitResultCoMap需要进行线程同步
            
            
        public:
            friend class Channel;
            friend class RpcClient;
            friend class Decoder;
            friend class Encoder;
        };
        
    public:
        class Channel : public google::protobuf::RpcChannel {
        private:
            class Guard {
            public:
                Guard(std::shared_ptr<ChannelCore> &channel): channel_(channel) {}
                ~Guard();
            private:
                std::shared_ptr<ChannelCore> channel_;
            };

        public:
            Channel(RpcClient *client, const std::string& host, uint32_t port, uint32_t connectNum = 1): channel_(new ChannelCore(client, host, port, connectNum)) {
                guard_ = std::make_shared<Guard>(channel_);
            }
            Channel(const Channel& channel) {
                channel_ = channel.channel_;
                guard_ = channel.guard_;
            }
            virtual ~Channel() {}

            virtual void CallMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller, const google::protobuf::Message *request, google::protobuf::Message *response, google::protobuf::Closure *done) {
                channel_->CallMethod(method, controller, request, response, done);
            }
            
            const std::string& getHost() const { return channel_->host_; }
            uint32_t getPort() const { return channel_->port_; }
            
        private:
            std::shared_ptr<Guard> guard_;
            std::shared_ptr<ChannelCore> channel_;
        };
        
        struct ConnectionTask {
            enum TaskType {CLOSE, CONNECT};
            TaskType type;
            std::shared_ptr<Connection> connection;
        };
        
#ifdef USE_NO_LOCK_QUEUE
        typedef Co_MPSC_NoLockQueue<ConnectionTask*> ConnectionTaskQueue;
        typedef Co_MPSC_NoLockQueue<std::shared_ptr<ClientTask>> ClientTaskQueue;
        typedef Co_MPSC_NoLockQueue<std::shared_ptr<ChannelCore>> ClearChannelQueue;
#else
        typedef CoSyncQueue<ConnectionTask*> ConnectionTaskQueue;
        typedef CoSyncQueue<std::shared_ptr<ClientTask>> ClientTaskQueue;
        typedef CoSyncQueue<std::shared_ptr<ChannelCore>> ClearChannelQueue;
#endif
       
    public:
        static RpcClient* create(IO *io);
        
    private:
        RpcClient(IO *io);
        
        ~RpcClient() {}
        
        static WorkerTask* decode(std::shared_ptr<corpc::Connection> &connection, uint8_t *head, uint8_t *body, int size);
        
        static bool encode(std::shared_ptr<corpc::Connection> &connection, std::shared_ptr<void>& data, uint8_t *buf, int space, int &size, std::string &downflowBuf, uint32_t &downflowBufSentNum);
        
        // 注意：需要开启线程来执行connectionRoutine和taskHandleRoutine协程，原因是not_care_response类型的rpc调用不会触发调用处协程切换，
        // 此时如果在同一线程中开启connectionRoutine和taskHandleRoutine协程，它们将等到调用处协程将来让出执行后才能得到调度，导致数据不能及时发送。
        static void threadEntry(RpcClient *self);
        
        static void *connectionRoutine(void * arg);  // 负责为connection连接建立和断线处理
        
        static void *taskHandleRoutine(void * arg);   // 负责将rpc调用请求通过connection交由sender发出
        
        static void *clearChannelRoutine(void * arg); // 用于清理Channel中的连接
        
        virtual void start();
        
    private:
        IO *io_;
        
        std::thread t_; // 任务处理线程
        
        ConnectionTaskQueue connectionTaskQueue_; // connectionRoutine处理的连接任务队列
        
        ClientTaskQueue taskQueue_; // taskHandleRoutine
        
        ClearChannelQueue clearChannelQueue_; // clearChannelRoutine
        
        PipelineFactory *pipelineFactory_;
    };
}

#endif /* corpc_rpc_client_h */
