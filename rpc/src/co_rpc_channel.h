//
//  co_rpc_channel.h
//  rpccli
//
//  Created by Xianke Liu on 2017/10/27.
//  Copyright © 2017年 Dena. All rights reserved.
//

#ifndef co_rpc_channel_h
#define co_rpc_channel_h

#include "co_routine.h"
#include "co_rpc_inner.h"

#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>

#include <list>
#include <map>
#include <stdio.h>

namespace CoRpc {
   
    class Client;
    class Channel;
    class Connection {
        enum Status {INIT, CLOSED, CONNECTING, CONNECTED};
        typedef std::list<ClientRpcTask*> WaitTaskList;
        typedef std::map<uint64_t, ClientRpcTask*> WaitTaskMap;
        
    public:
        Connection(Channel *channel): _channel(channel), _fd(-1), _st(INIT), _deamonCo(NULL) {}
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
        bool _deamonHang; // deamon协程是否挂起
        stCoRoutine_t* _deamonCo; // deamon协程
        
        WaitTaskList _waitSendTaskCoList;// 等待发送RPC请求的任务
        WaitTaskMap _waitResultCoMap; // 等待接受RPC结果的任务
        
    public:
        friend class Channel;
        friend class Client;
    };
    
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
        friend class Connection;
        friend class Client;
    };
}

#endif /* co_rpc_channel_h */
