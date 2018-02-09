//
//  co_rpc_inner.h
//  rpcsvr
//
//  Created by Xianke Liu on 2017/11/8.
//  Copyright © 2017年 Dena. All rights reserved.
//

#ifndef co_rpc_inner_h
#define co_rpc_inner_h

#include "co_routine.h"
#include "co_routine_inner.h"
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <atomic>
#include <mutex>
#include <list>
#include <vector>
#include <unistd.h>

#define USE_NO_LOCK_QUEUE

namespace CoRpc {
    
    struct RpcResponseHead {
        uint32_t size;
        uint64_t callId;
    };
    
    struct RpcRequestHead {
        uint32_t size;
        uint32_t serviceId;
        uint32_t methodId;
        uint64_t callId;
    };
    
    class Channel;
    
    struct PipeType {
        int pipefd[2];
    };
    
    // 多生产者单消费者无锁队列实现
    template <typename T, T defaultValue>
    class MPSC_NoLockQueue {
        struct Node {
            T value;
            Node *next;
        };
        
    public:
        MPSC_NoLockQueue():_head(NULL), _outqueue(NULL) {}
        ~MPSC_NoLockQueue() {}
        
        void push(T & v) {
            Node *newNode = new Node;
            newNode->value = v;
            
            do {
                newNode->next = _head;
            } while (!_head.compare_exchange_weak(newNode->next, newNode));
        }
        
        T pop() {
            T ret = defaultValue;
        
            if (!_outqueue) {
                if (_head != NULL) {
                    do {
                        _outqueue = _head;
                    } while (!_head.compare_exchange_weak(_outqueue, NULL));
                    
                    // 翻转
                    Node *n1 = _outqueue;
                    _outqueue = NULL;
                    while (n1) {
                        Node *n2 = n1->next;
                        n1->next = _outqueue;
                        _outqueue = n1;
                        n1 = n2;
                    }
                }
            }
            
            if (_outqueue) {
                ret = _outqueue->value;
                Node *tnode = _outqueue;
                _outqueue = _outqueue->next;
                delete tnode;
            }
            
            return ret;
        }
            
    private:
        std::atomic<Node*> _head;
        Node *_outqueue;
    };
    
    template <typename T, T defaultValue>
    class Co_MPSC_NoLockQueue: public MPSC_NoLockQueue<T, defaultValue> {
    public:
        Co_MPSC_NoLockQueue() { pipe(_queuePipe.pipefd); }
        ~Co_MPSC_NoLockQueue() {
            close(_queuePipe.pipefd[0]);
            close(_queuePipe.pipefd[1]);
        }
        
        int getReadFd() { return _queuePipe.pipefd[0]; }
        int getWriteFd() { return _queuePipe.pipefd[1]; }
        
        void push(T & v) {
            MPSC_NoLockQueue<T, defaultValue>::push(v);
            
            char buf = 'A';
            write(getWriteFd(), &buf, 1);
        }
        
    private:
        PipeType _queuePipe; // 管道（用于通知处理协程有新rpc任务入队）
    };

    // 注意：该Queue实现只支持多生产者和单消费者情形
    template <typename T, T defaultValue>
    class SyncQueue {
    public:
        SyncQueue() {}
        ~SyncQueue() {}
        
        void push(T & v) {
            std::unique_lock<std::mutex> lock( _queueMutex );
            _inqueue.push_back(v);
        }
        
        T pop() {
            T ret = defaultValue;
            
            if (!_outqueue.empty()) {
                ret = _outqueue.front();
                
                _outqueue.pop_front();
            } else {
                if (!_inqueue.empty()) {
                    {
                        std::unique_lock<std::mutex> lock( _queueMutex );
                        _inqueue.swap(_outqueue);
                    }
                    
                    ret = _outqueue.front();
                    
                    _outqueue.pop_front();
                }
            }
            
            return ret;
        }
        
    private:
        std::mutex _queueMutex;
        std::list<T> _inqueue;
        std::list<T> _outqueue;
    };
    
    template <typename T, T defaultValue>
    class CoSyncQueue: public SyncQueue<T, defaultValue> {
    public:
        CoSyncQueue() { pipe(_queuePipe.pipefd); }
        ~CoSyncQueue() {
            close(_queuePipe.pipefd[0]);
            close(_queuePipe.pipefd[1]);
        }
        
        int getReadFd() { return _queuePipe.pipefd[0]; }
        int getWriteFd() { return _queuePipe.pipefd[1]; }
        
        void push(T & v) {
            SyncQueue<T, defaultValue>::push(v);
            
            char buf = 'A';
            ssize_t ret = write(getWriteFd(), &buf, 1);
            assert(ret == 1);
        }
        
    private:
        PipeType _queuePipe; // 管道（用于通知处理协程有新rpc任务入队）
    };
    
    struct ClientRpcTask {
        Channel *channel;
        stCoRoutine_t *co;
        const google::protobuf::Message* request;
        google::protobuf::Message* response;
        google::protobuf::RpcController *controller;
        uint32_t serviceId;
        uint32_t methodId;
    };
    
    struct ServerRpcTask {
        google::protobuf::Service *service;
        const google::protobuf::MethodDescriptor *method_descriptor;
        const google::protobuf::Message* request;
        google::protobuf::Message* response;
        google::protobuf::RpcController *controller;
        uint64_t callId;
    };
    
#ifdef USE_NO_LOCK_QUEUE
    typedef Co_MPSC_NoLockQueue<ClientRpcTask*, static_cast<struct ClientRpcTask *>(NULL)> ClientRpcTaskQueue;

    typedef Co_MPSC_NoLockQueue<int, 0> ServerConnectFdQueue; // 用于从Acceptor向Receiver传递建立的连接fd
    
    typedef MPSC_NoLockQueue<ServerRpcTask*, static_cast<struct ServerRpcTask *>(NULL)> ServerRpcResultQueue; // 用于从worker向rpc收发协程发送rpc处理结果
#else
    typedef CoSyncQueue<ClientRpcTask*, static_cast<struct ClientRpcTask *>(NULL)> ClientRpcTaskQueue;
    
    typedef CoSyncQueue<int, 0> ServerConnectFdQueue; // 用于从Acceptor向Receiver传递建立的连接fd
    
    typedef SyncQueue<ServerRpcTask*, static_cast<struct ServerRpcTask *>(NULL)> ServerRpcResultQueue; // 用于从worker向rpc收发协程发送rpc处理结果
#endif
    
    struct ServerWorkerTask {
        std::shared_ptr<ServerRpcResultQueue> queue; // 处理结果将存入此队列
        
        ServerRpcTask *rpcTask;
    };
    
#ifdef USE_NO_LOCK_QUEUE
    typedef Co_MPSC_NoLockQueue<ServerWorkerTask*, static_cast<struct ServerWorkerTask *>(NULL)> ServerWorkerTaskQueue; // 用于从rpc收发协程向worker发送rpc任务（注意：用于pipe通知版本）
    //typedef MPSC_NoLockQueue<ServerWorkerTask*, static_cast<struct ServerWorkerTask *>(NULL)> ServerWorkerTaskQueue; // 用于从rpc收发协程向worker发送rpc任务（注意：用于轮询版本）
#else
    typedef CoSyncQueue<ServerWorkerTask*, static_cast<struct ServerWorkerTask *>(NULL)> ServerWorkerTaskQueue; // 用于从rpc收发协程向worker发送rpc任务（注意：用于pipe通知版本）
    //typedef SyncQueue<ServerWorkerTask*, static_cast<struct ServerWorkerTask *>(NULL)> ServerWorkerTaskQueue; // 用于从rpc收发协程向worker发送rpc任务（注意：用于轮询版本）
#endif
    
    struct MethodData {
        const google::protobuf::MethodDescriptor *_method_descriptor;
        const google::protobuf::Message *_request_proto;
        const google::protobuf::Message *_response_proto;
    };
    
    struct ServiceData {
        google::protobuf::Service *rpcService;
        std::vector<MethodData> methods;
    };
    
    struct DebugContext {
        // the params below are for debug
        int iSuccCnt = 0;
        int iFailCnt = 0;
        int iTime = 0;
        
        void AddSuccCnt(const char * pref)
        {
            int now = time(NULL);
            if (now >iTime)
            {
                printf("%s Co:%llu time %d Succ Cnt %d Fail Cnt %d\n", pref, GetCurrThreadCo(), iTime, iSuccCnt, iFailCnt);
                iTime = now;
                iSuccCnt = 0;
                iFailCnt = 0;
            }
            else
            {
                iSuccCnt++;
            }
        }
        void AddFailCnt(const char * pref)
        {
            int now = time(NULL);
            if (now >iTime)
            {
                printf("%s Co:%llu time %d Succ Cnt %d Fail Cnt %d\n", pref, GetCurrThreadCo(), iTime, iSuccCnt, iFailCnt);
                iTime = now;
                iSuccCnt = 0;
                iFailCnt = 0;
            }
            else
            {
                iFailCnt++;
            }
        }
    };
}

#endif /* co_rpc_inner_h */
