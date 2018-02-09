//
//  co_rpc_client.h
//  rpccli
//
//  Created by Xianke Liu on 2017/11/1.
//  Copyright © 2017年 Dena. All rights reserved.
//

#ifndef co_rpc_client_h
#define co_rpc_client_h

#include "co_rpc_channel.h"

#include <queue>
#include <mutex>
#include <thread>
#include <unistd.h>

#include <google/protobuf/service.h>

// TODO: 启动1条线程协程化处理rpc请求收发
// rpc channel需要注册到client中才能被服务
namespace CoRpc {
    
    typedef std::map<Channel*, Channel*> ChannelSet;
    
    class Client {
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
        ClientRpcTaskQueue _upQueue; // 上行任务队列（rpc请求线程往rpc处理线程发rpc处理请求）
        ClientRpcTaskQueue _downQueue; // 下行任务队列（rpc处理线程往rpc请求线程发rpc处理结果）
        
        ChannelSet _channelSet; // 注册的channel
        
    public:
        friend class Connection;
        friend class Channel;
    };
}

#endif /* co_rpc_client_h */
