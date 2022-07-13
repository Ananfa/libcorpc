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

#include "corpc_mutex.h"
#include "corpc_utils.h"

#define USE_NO_LOCK_QUEUE

//#define MONITOR_ROUTINE
#define CORPC_MAX_BUFFER_SIZE 0x10000
#define CORPC_MAX_REQUEST_SIZE 0x10000
#define CORPC_MAX_RESPONSE_SIZE 0x100000

#define CORPC_REQUEST_HEAD_SIZE 28
#define CORPC_RESPONSE_HEAD_SIZE 20

// message head format
// |body size(4 bytes)|message type(2 bytes)|tag(2 byte)|flag(2 byte)|req serial number(4 bytes)|serial number(4 bytes)|crc(2 bytes)|
#define CORPC_MESSAGE_HEAD_SIZE 20
#define CORPC_MAX_MESSAGE_SIZE 0x10000
#define CORPC_MAX_UDP_MESSAGE_SIZE 540

#define CORPC_MESSAGE_FLAG_CRYPT 0x1

#define CORPC_MSG_TYPE_CONNECT -1
#define CORPC_MSG_TYPE_CLOSE -2
#define CORPC_MSG_TYPE_BANNED -10
#define CORPC_MSG_TYPE_UDP_UNSHAKE -110
#define CORPC_MSG_TYPE_UDP_HANDSHAKE_1 -111
#define CORPC_MSG_TYPE_UDP_HANDSHAKE_2 -112
#define CORPC_MSG_TYPE_UDP_HANDSHAKE_3 -113
#define CORPC_MSG_TYPE_UDP_HANDSHAKE_4 -114
#define CORPC_MSG_TYPE_HEARTBEAT -115

#define CORPC_HEARTBEAT_PERIOD 5000
#define CORPC_MAX_NO_HEARTBEAT_TIME 15000

#define CORPC_MAX_UINT64 0xFFFFFFFFFFFFFFFF
#define CORPC_MIN_UINT64 0
#define CORPC_SKIP_LIST_MAX_LEVEL 16

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

// 注意：该实现没有考虑PDP_ENDIAN情形
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
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

#if ( __i386__ || __i386 || __amd64__ || __amd64 )
#define corpc_cpu_pause()         __asm__ ("pause")
#elif defined(__x86_64__)
#define corpc_cpu_pause()         __asm__ (".byte 0xf3, 0x90")
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
        int16_t type;
        bool isRaw;
        bool needCrypt;
        uint16_t tag;
        uint32_t serial;
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
            while (!_head.compare_exchange_weak(newNode->next, newNode));
        }
        
        void push(T&& v) {
            Node *newNode = new Node;
            newNode->value = std::move(v);
            
            newNode->next = _head;
            while (!_head.compare_exchange_weak(newNode->next, newNode));
        }
        
        T pop() {
            T ret(nullptr);
            
            if (!_outqueue) {
                if (_head != NULL) {
                    _outqueue = _head;
                    while (!_head.compare_exchange_weak(_outqueue, NULL));
                    
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

    // 另一种形式的非锁实现
    // 注意：该Queue实现只支持多生产者和单消费者情形（因为_outqueue不是线程安全）
    template <typename T>
    class SyncQueue {
    public:
        SyncQueue() {}
        ~SyncQueue() {}
        
        void push(T & v) {
            LockGuard lock( _queueMutex );
            _inqueue.push_back(v);
        }
        
        void push(T && v) {
            LockGuard lock( _queueMutex );
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
                        LockGuard lock( _queueMutex );
                        _inqueue.swap(_outqueue);
                    }
                    
                    ret = std::move(_outqueue.front());
                    
                    _outqueue.pop_front();
                }
            }
            
            return ret;
        }
        
    private:
        Mutex _queueMutex;
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
    
     // 注意：该Queue实现只支持多生产者和单消费者情形
    template <typename T>
    class LockQueue {
    public:
        LockQueue() {}
        ~LockQueue() {}
        
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
    class CoLockQueue: public LockQueue<T> {
    public:
        CoLockQueue() {
            pipe(_queuePipe.pipefd);
            
            co_register_fd(_queuePipe.pipefd[1]);
            co_set_nonblock(_queuePipe.pipefd[1]);
        }
        ~CoLockQueue() {
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

    template <typename T>
    class NormalLink {
    public:
        class Node {
        public:
            std::shared_ptr<T> data;
            
            Node *next;
            Node *prev;
        };
        
        class Iterator {
            Node* ptr;
            
            // implement explicit copy construct (c++11 or later)
            //explicit Iterator(NodeType* nd): ptr(nd) {}
            Iterator(Node* nd): ptr(nd) {}
            
        public:
            Iterator(): ptr(NULL) {}
            
            Iterator(const Iterator& rhs): ptr(rhs.ptr) {}
            
            // swap (c++11 or later)
            // void swap(Iterator& other) noexcept
            // {
            //     using std::swap;
            //     swap(ptr, other.ptr);
            // }
            
            Iterator& operator++ () {
                assert(ptr != NULL && "Out-of-bounds iterator increment!");
                ptr = ptr->next;
                return *this;
            }
            
            Iterator operator++ (int) {
                assert(ptr != NULL && "Out-of-bounds iterator increment!");
                Iterator tmp(*this);
                ptr = ptr->next;
                return tmp;
            }
            
            bool operator == (const Iterator& rhs) const {
                return ptr == rhs.ptr;
            }
            
            bool operator != (const Iterator& rhs) const {
                return ptr != rhs.ptr;
            }
            
            Node& operator* () const {
                assert(ptr != NULL && "Out-of-bounds iterator increment!");
                return *ptr;
            }
            
            Node* operator-> () const {
                assert(ptr != NULL && "Out-of-bounds iterator increment!");
                return ptr;
            }
            
        public:
            friend class NormalLink<T>;
        };

    public:
        NormalLink(): _head(nullptr), _tail(nullptr), _eIter(nullptr) {}
        ~NormalLink() {
            clear();
        }
        
        void push_back(Node *node);
        Node* pop_front();
        void erase(Node *node);
        void eraseTo(Node *node);
        void moveToTail(Node *node);
        void clear();
        
        Node* getHead() { return _head; }
        Node* getTail() { return _tail; }
        
        Iterator begin() const {
            Iterator iter(_head);
            return iter;
        }

        const Iterator& end() const { return _eIter; }

    private:
        Node *_head;
        Node *_tail;

        Iterator _eIter;
    };

    template <typename T>
    void NormalLink<T>::push_back(Node *node) {
        if (_head) {
            assert(_tail && !_tail->next);
            node->next = nullptr;
            node->prev = _tail;
            _tail->next = node;
            _tail = node;
        } else {
            assert(!_tail);
            node->next = nullptr;
            node->prev = nullptr;
            _head = node;
            _tail = node;
        }
    }

    template <typename T>
    typename NormalLink<T>::Node* NormalLink<T>::pop_front() {
        if (!_head) {
            return nullptr;
        }
        
        Node *node = _head;
        _head = _head->next;
        if (_head) {
            _head->prev = nullptr;
        } else {
            assert(_tail == node);
            _tail = nullptr;
        }
        
        node->next = nullptr;
        node->prev = nullptr;
        
        return node;
    }

    template <typename T>
    void NormalLink<T>::erase(Node *node) {
        Node *prev = node->prev;
        Node *next = node->next;
        
        if (prev) {
            prev->next = next;
        }
        
        if (next) {
            next->prev = prev;
        }
        
        if (_head == node) {
            _head = next;
        }
        
        if (_tail == node) {
            _tail = prev;
        }
        
        delete node;
    }

    template <typename T>
    void NormalLink<T>::eraseTo(Node *node) {
        assert(node != nullptr);
        Node *tmpNode = _head;

        while (tmpNode != node) {
            assert(tmpNode != nullptr);
            Node *next = tmpNode->next;
            delete tmpNode;
            tmpNode = next;
        }

        _head = node->next;
        if (_head) {
            _head->prev = nullptr;
        } else {
            assert(_tail == node);
            _tail = nullptr;
        }

        delete node;
    }

    template <typename T>
    void NormalLink<T>::moveToTail(Node *node) {
        Node *next = node->next;
        
        assert(next || _tail == node);
        
        if (next) {
            Node *prev = node->prev;
            next->prev = prev;
            
            if (prev) {
                prev->next = next;
            }
            
            if (_head == node) {
                _head = next;
            }
            
            node->next = nullptr;
            node->prev = _tail;
            _tail->next = node;
            _tail = node;
        }
    }

    template <typename T>
    void NormalLink<T>::clear() {
        Node *node = _head;
        while (node) {
            Node *next = node->next;
            delete node;
            node = next;
        }
        
        _head = nullptr;
        _tail = nullptr;
    }

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
                LOG("%s Co:%llu time %d Succ Cnt %d Fail Cnt %d\n", pref, GetCurrThreadCo(), iTime, iSuccCnt, iFailCnt);
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
                LOG("%s Co:%llu time %d Succ Cnt %d Fail Cnt %d\n", pref, GetCurrThreadCo(), iTime, iSuccCnt, iFailCnt);
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
