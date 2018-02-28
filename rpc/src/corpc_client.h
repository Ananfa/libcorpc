//
//  co_rpc_client.h
//  rpccli
//
//  Created by Xianke Liu on 2017/11/1.
//  Copyright © 2017年 Dena. All rights reserved.
//

#ifndef co_rpc_client_h
#define co_rpc_client_h

#include "corpc_io.h"

#include <list>
#include <map>
#include <queue>
#include <mutex>
#include <unistd.h>

#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>

#define CORPC_MAX_RESPONSE_SIZE 0x100000

// TODO: 启动1条线程协程化处理rpc请求收发
// rpc channel需要注册到client中才能被服务
namespace CoRpc {
    
    class Client {
        
        struct RpcTask {
            stCoRoutine_t *co;
            const google::protobuf::Message* request;
            google::protobuf::Message* response;
            google::protobuf::RpcController *controller;
            uint32_t serviceId;
            uint32_t methodId;
        };
        
        struct ClientTask {
            google::protobuf::RpcChannel *channel;
            std::shared_ptr<RpcTask> rpcTask;
        };
        
#ifdef USE_NO_LOCK_QUEUE
        typedef Co_MPSC_NoLockQueue<ClientTask*, static_cast<struct ClientTask *>(NULL)> ClientTaskQueue;
#else
        typedef CoSyncQueue<ClientTask*, static_cast<struct ClientTask *>(NULL)> ClientTaskQueue;
#endif
        
    public:
        class Channel;
    private:
        class Connection: public IO::Connection {
            enum Status {CLOSED, CONNECTING, CONNECTED};
            typedef std::list<ClientTask*> WaitTaskList;
            typedef std::map<uint64_t, ClientTask*> WaitTaskMap;
            
        public:
            Connection(Channel *channel);
            ~Connection() {}
            
            virtual bool parseData(uint8_t *buf, int size);
            virtual int buildData(uint8_t *buf, int space);
            
            virtual void onClose();
            
        private:
            Channel *_channel;
            
            Status _st;
            
            WaitTaskList _waitSendTaskCoList;// 等待发送RPC请求的任务
            WaitTaskMap _waitResultCoMap; // 等待接受RPC结果的任务
            std::mutex _waitResultCoMapMutex; // _waitResultCoMap需要进行线程同步
            
            // 接收数据时的包头和包体
            RpcResponseHead _resphead;
            char *_head_buf;
            int _headNum;
            
            std::string _respdata;
            uint8_t *_data_buf;
            int _dataNum;
            
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
            std::shared_ptr<Connection>& getNextConnection();
            
        private:
            std::string _ip;
            uint32_t _port;
            
            Client *_client;
            std::vector<std::shared_ptr<Connection>> _connections;
            uint32_t _conIndex;
            
        public:
            friend class ClientConnection;
            friend class Client;
        };
        
        typedef std::map<Channel*, Channel*> ChannelSet;
        
    public:
        Client(IO *io): _io(io), _connectRoutineHang(false), _connectDelay(false), _connectRoutine(NULL), _upRoutineHang(false), _upRoutine(NULL) {}
        
        bool registerChannel(Channel *channel);
        
        void start();
    
        void destroy() { delete this; } // 销毁Client
        
    private:
        ~Client() {}
        
        static void *connectRoutine( void * arg );  // 负责为connection连接建立
        
        static void *upRoutine( void * arg );   // 负责将rpc调用请求通过connection交由sender发出
        
        static void *downRoutine( void * arg ); // 负责接收rpc结果并唤醒rpc调用协程
        
    private:
        IO *_io;
        
        std::list<std::shared_ptr<Connection>> _waitConnections; // 等待建立连接的Connection
        bool _connectRoutineHang; // 连接建立分派协程是否挂起
        bool _connectDelay; // 是否需要延迟连接
        stCoRoutine_t* _connectRoutine; // 连接建立分派协程
        
        // rpc task queue
        // 由于upRoutine是在Client所在的线程中(即与rpc调用发起在同一线程中)，可以改造upRoutine实现，通过co_yield_ct和co_resume来控制routine执行，而不需要用pipe-fd来进行通知
        std::list<ClientTask*> _upList;
        bool _upRoutineHang; // 上行协程是否挂起
        stCoRoutine_t* _upRoutine; // 上行协程
        
        ClientTaskQueue _downQueue; // 处理结果队列（rpc处理线程往rpc请求线程发rpc处理结果）
        
        ChannelSet _channelSet; // 注册的channel
    };
}

#endif /* co_rpc_client_h */
