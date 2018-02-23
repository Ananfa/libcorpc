//
//  co_rpc_client.h
//  rpccli
//
//  Created by Xianke Liu on 2017/11/1.
//  Copyright © 2017年 Dena. All rights reserved.
//

#ifndef co_rpc_client_h
#define co_rpc_client_h

#include "co_routine.h"
#include "corpc_inner.h"

#include <list>
#include <map>
#include <queue>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <stdio.h>

#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>

// TODO: 启动1条线程协程化处理rpc请求收发
// rpc channel需要注册到client中才能被服务
namespace CoRpc {
    
    class Client {
        
        struct RpcTask {
            google::protobuf::RpcChannel *channel;
            stCoRoutine_t *co;
            const google::protobuf::Message* request;
            google::protobuf::Message* response;
            google::protobuf::RpcController *controller;
            uint32_t serviceId;
            uint32_t methodId;
        };
        
#ifdef USE_NO_LOCK_QUEUE
        typedef Co_MPSC_NoLockQueue<RpcTask*, static_cast<struct RpcTask *>(NULL)> RpcTaskQueue;
#else
        typedef CoSyncQueue<RpcTask*, static_cast<struct RpcTask *>(NULL)> RpcTaskQueue;
#endif
        
    public:
        class Channel;
    private:
        class Connection {
            enum Status {INIT, CLOSED, CONNECTING, CONNECTED};
            typedef std::list<RpcTask*> WaitTaskList;
            typedef std::map<uint64_t, RpcTask*> WaitTaskMap;
            
        public:
            Connection(Channel *channel): _channel(channel), _fd(-1), _st(INIT), _routine(NULL) {}
            ~Connection() {}
            
        private:
            static void *workingRoutine( void * arg );
            
            void start(const stCoRoutineAttr_t *attr);
            
            void handleTaskError();
            
            void wakeUpAll(WaitTaskList& taskList, int err);
            void wakeUpAll(WaitTaskMap& taskMap, int err);
            
        private:
            Channel *_channel;
            
            int _fd; // connect fd
            Status _st;
            bool _routineHang; // 处理协程是否挂起
            stCoRoutine_t* _routine; // 处理协程
            
            WaitTaskList _waitSendTaskCoList;// 等待发送RPC请求的任务
            WaitTaskMap _waitResultCoMap; // 等待接受RPC结果的任务
            
        public:
            friend class Channel;
            friend class Client;
        };
        
    public:
        class Channel : public google::protobuf::RpcChannel {
        public:
            Channel(Client *client, const char *ip, uint32_t port, uint32_t connectNum = 1);
            virtual ~Channel();
            virtual void CallMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller, const google::protobuf::Message *request, google::protobuf::Message *response, google::protobuf::Closure *done);
            
        private:
            void start(const stCoRoutineAttr_t *attr);
            Connection *getNextConnection();
            
        private:
            std::string _ip;
            uint32_t _port;
            
            Client *_client;
            std::vector<Connection *> _connections;
            uint32_t _conIndex;
            
        public:
            friend class ClientConnection;
            friend class Client;
        };
        
        typedef std::map<Channel*, Channel*> ChannelSet;
        
    public:
        Client() {}
        ~Client() {}
        
        bool registerChannel(Channel *channel);
        
        void start();
    
    private:
        static void threadEntry( Client *self );
        
        static void *upQueueRoutine( void * arg );
        
        static void *downQueueRoutine( void * arg );
        
    private:
        std::thread _t; // rpc处理线程
        
        // rpc task queue
        RpcTaskQueue _upQueue; // 上行任务队列（rpc请求线程往rpc处理线程发rpc处理请求）
        RpcTaskQueue _downQueue; // 下行任务队列（rpc处理线程往rpc请求线程发rpc处理结果）
        
        ChannelSet _channelSet; // 注册的channel
    };
}

#endif /* co_rpc_client_h */
