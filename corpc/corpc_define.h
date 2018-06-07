/*
 * Created by Xianke Liu on 2017/11/8.
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

#ifndef corpc_define_h
#define corpc_define_h

#include "co_routine.h"
#include "co_routine_inner.h"
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <atomic>
#include <mutex>
#include <list>
#include <vector>
#include <memory>
#include <unistd.h>

#define USE_NO_LOCK_QUEUE

//#define MONITOR_ROUTINE

#define CORPC_MAX_BUFFER_SIZE 0x10000
#define CORPC_MAX_REQUEST_SIZE 0x10000
#define CORPC_MAX_RESPONSE_SIZE 0x100000

#define CORPC_REQUEST_HEAD_SIZE 20
#define CORPC_RESPONSE_HEAD_SIZE 12

#define CORPC_MESSAGE_HEAD_SIZE 8
#define CORPC_MAX_MESSAGE_SIZE 0x10000

#define CORPC_MAX_UDP_MESSAGE_SIZE 540

#define CORPC_MSG_TYPE_CONNECT -1
#define CORPC_MSG_TYPE_CLOSE -2
#define CORPC_MSG_TYPE_UDP_HANDSHAKE_1 -111
#define CORPC_MSG_TYPE_UDP_HANDSHAKE_2 -112
#define CORPC_MSG_TYPE_UDP_HANDSHAKE_3 -113
#define CORPC_MSG_TYPE_HEARTBEAT -115

#define CORPC_HEARTBEAT_PERIOD 10000
#define CORPC_MAX_NO_HEARTBEAT_TIME 30000


#define bswap16(_x)        (uint16_t)((_x) << 8 | (_x) >> 8)

#define bswap32(_x)                     \
    (((_x) >> 24) |                     \
    (((_x) & (0xff << 16)) >> 8) |      \
    (((_x) & (0xff << 8)) << 8) |       \
    ((_x) << 24))

#define bswap64(_x)                     \
    (((_x) >> 56) |                     \
    (((_x) >> 40) & (0xffUL << 8)) |    \
    (((_x) >> 24) & (0xffUL << 16)) |   \
    (((_x) >> 8) & (0xffUL << 24)) |    \
    (((_x) << 8) & (0xffUL << 32)) |    \
    (((_x) << 24) & (0xffUL << 40)) |   \
    (((_x) << 40) & (0xffUL << 48)) |   \
    ((_x) << 56))

#if ( 1>>1 == 0 )
    // Little Endian
    #ifndef htobe16
        #define    htobe16(x)    bswap16((uint16_t)(x))
    #endif
    #ifndef htobe32
        #define    htobe32(x)    bswap32((uint32_t)(x))
    #endif
    #ifndef htobe64
        #define    htobe64(x)    bswap64((uint64_t)(x))
    #endif
    #ifndef htole16
        #define    htole16(x)    ((uint16_t)(x))
    #endif
    #ifndef htole32
        #define    htole32(x)    ((uint32_t)(x))
    #endif
    #ifndef htole64
        #define    htole64(x)    ((uint64_t)(x))
    #endif
    #ifndef be16toh
        #define    be16toh(x)    bswap16((uint16_t)(x))
    #endif
    #ifndef be32toh
        #define    be32toh(x)    bswap32((uint32_t)(x))
    #endif
    #ifndef be64toh
        #define    be64toh(x)    bswap64((uint64_t)(x))
    #endif
    #ifndef le16toh
        #define    le16toh(x)    ((uint16_t)(x))
    #endif
    #ifndef le32toh
        #define    le32toh(x)    ((uint32_t)(x))
    #endif
    #ifndef le64toh
        #define    le64toh(x)    ((uint64_t)(x))
    #endif
#else
    // Big Endian
    #ifndef htobe16
        #define    htobe16(x)    ((uint16_t)(x))
    #endif
    #ifndef htobe32
        #define    htobe32(x)    ((uint32_t)(x))
    #endif
    #ifndef htobe64
        #define    htobe64(x)    ((uint64_t)(x))
    #endif
    #ifndef htole16
        #define    htole16(x)    bswap16((uint16_t)(x))
    #endif
    #ifndef htole32
        #define    htole32(x)    bswap32((uint32_t)(x))
    #endif
    #ifndef htole64
        #define    htole64(x)    bswap64((uint64_t)(x))
    #endif
    #ifndef be16toh
        #define    be16toh(x)    ((uint16_t)(x))
    #endif
    #ifndef be32toh
        #define    be32toh(x)    ((uint32_t)(x))
    #endif
    #ifndef be64toh
        #define    be64toh(x)    ((uint64_t)(x))
    #endif
    #ifndef le16toh
        #define    le16toh(x)    bswap16((uint16_t)(x))
    #endif
    #ifndef le32toh
        #define    le32toh(x)    bswap32((uint32_t)(x))
    #endif
    #ifndef le64toh
        #define    le64toh(x)    bswap64((uint64_t)(x))
    #endif
#endif

namespace corpc {
    
    struct PipeType {
        int pipefd[2];
    };
    
    struct MethodData {
        const google::protobuf::MethodDescriptor *method_descriptor;
        const google::protobuf::Message *request_proto;
        const google::protobuf::Message *response_proto;
    };
    
    struct ServiceData {
        google::protobuf::Service *rpcService;
        std::vector<MethodData> methods;
    };
    
    struct SendMessageInfo {
        int32_t type;
        bool isRaw;
        std::shared_ptr<void> msg;  // 当isRaw为true时，msg中存的是std::string指针，当isRaw为false时，msg中存的是google::protobuf::Message指针。这是为了广播或转发消息给玩家时不需要对数据进行protobuf编解码
    };
    
    // 多生产者单消费者无锁队列实现
    // 注意：该实现只能在gcc 4.8.3之上版本使用，否则会出错，详见（https://gcc.gnu.org/bugzilla/show_bug.cgi?id=60272）
    template <typename T>
    class MPSC_NoLockQueue {
        struct Node {
            T value;
            Node *next;
        };
        
    public:
        MPSC_NoLockQueue():_head(NULL), _outqueue(NULL) {}
        ~MPSC_NoLockQueue() {}
        
        void push(T& v) {
            Node *newNode = new Node;
            newNode->value = v;
            
            newNode->next = _head;
            do {
            } while (!_head.compare_exchange_weak(newNode->next, newNode));
        }
        
        void push(T&& v) {
            Node *newNode = new Node;
            newNode->value = std::move(v);
            
            newNode->next = _head;
            do {
            } while (!_head.compare_exchange_weak(newNode->next, newNode));
        }
        
        T pop() {
            T ret(nullptr);
        
            if (!_outqueue) {
                if (_head != NULL) {
                    _outqueue = _head;
                    do {
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
                ret = std::move(_outqueue->value);
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
    
    template <typename T>
    class Co_MPSC_NoLockQueue: public MPSC_NoLockQueue<T> {
    public:
        Co_MPSC_NoLockQueue() {
            pipe(_queuePipe.pipefd);
            
            co_register_fd(_queuePipe.pipefd[1]);
            co_set_nonblock(_queuePipe.pipefd[1]);
        }
        ~Co_MPSC_NoLockQueue() {
            close(_queuePipe.pipefd[0]);
            close(_queuePipe.pipefd[1]);
        }
        
        int getReadFd() { return _queuePipe.pipefd[0]; }
        int getWriteFd() { return _queuePipe.pipefd[1]; }
        
        void push(T & v) {
            MPSC_NoLockQueue<T>::push(v);
            
            char buf = 'X';
            write(getWriteFd(), &buf, 1);
        }
        
        void push(T && v) {
            MPSC_NoLockQueue<T>::push(std::move(v));
            
            char buf = 'X';
            write(getWriteFd(), &buf, 1);
        }
        
    private:
        PipeType _queuePipe; // 管道（用于通知处理协程有新rpc任务入队）
    };

    // 注意：该Queue实现只支持多生产者和单消费者情形
    template <typename T>
    class SyncQueue {
    public:
        SyncQueue() {}
        ~SyncQueue() {}
        
        void push(T & v) {
            std::unique_lock<std::mutex> lock( _queueMutex );
            _inqueue.push_back(v);
        }
        
        void push(T && v) {
            std::unique_lock<std::mutex> lock( _queueMutex );
            _inqueue.push_back(std::move(v));
        }
        
        T pop() {
            T ret(nullptr);
            
            if (!_outqueue.empty()) {
                ret = std::move(_outqueue.front());
                
                _outqueue.pop_front();
            } else {
                if (!_inqueue.empty()) {
                    {
                        std::unique_lock<std::mutex> lock( _queueMutex );
                        _inqueue.swap(_outqueue);
                    }
                    
                    ret = std::move(_outqueue.front());
                    
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
    
    template <typename T>
    class CoSyncQueue: public SyncQueue<T> {
    public:
        CoSyncQueue() {
            pipe(_queuePipe.pipefd);
            
            co_register_fd(_queuePipe.pipefd[1]);
            co_set_nonblock(_queuePipe.pipefd[1]);
        }
        ~CoSyncQueue() {
            close(_queuePipe.pipefd[0]);
            close(_queuePipe.pipefd[1]);
        }
        
        int getReadFd() { return _queuePipe.pipefd[0]; }
        int getWriteFd() { return _queuePipe.pipefd[1]; }
        
        void push(T & v) {
            SyncQueue<T>::push(v);
            
            char buf = 'K';
            write(getWriteFd(), &buf, 1);
        }
        
        void push(T && v) {
            SyncQueue<T>::push(std::move(v));
            
            char buf = 'K';
            write(getWriteFd(), &buf, 1);
        }
        
    private:
        PipeType _queuePipe; // 管道（用于通知处理协程有新rpc任务入队）
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

#endif /* corpc_define_h */
