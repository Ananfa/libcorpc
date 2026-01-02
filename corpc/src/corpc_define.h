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
#define CORPC_MAX_REQUEST_SIZE 0x100000
#define CORPC_MAX_RESPONSE_SIZE 0x100000

// rpc request head format
// |body size(4 bytes)|service id(4 bytes)|method id(4 bytes)|coroutine ptr(8 bytes)|expire time(8 bytes)|
#define CORPC_REQUEST_HEAD_SIZE 28
// rpc response head format
// |body size(4 bytes)|coroutine ptr(8 bytes)|expire time(8 bytes)|error code(4 bytes)|
#define CORPC_RESPONSE_HEAD_SIZE 24
// message head format
// |body size(4 bytes)|message type(4 bytes)|tag(2 byte)|flag(2 byte)|req serial number(4 bytes)|serial number(4 bytes)|crc(2 bytes)|
// 注意：消息头部长度CORPC_MESSAGE_HEAD_SIZE不能大于等于24，因为那样会与kcp消息冲突
#define CORPC_MESSAGE_HEAD_SIZE 22
// udp handshake message format
// |message type(4 bytes)|
#define CORPC_UDP_HANDSHAKE_SIZE 4

#define CORPC_MAX_MESSAGE_SIZE 0x10000
#define CORPC_MAX_UDP_MESSAGE_SIZE 540
#define CORPC_MAX_KCP_PACKAGE_SIZE 0x1000

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
#define CORPC_MSG_TYPE_JUMP_SERIAL -116

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
        int32_t type;
        bool isRaw;
        bool needCrypt;
        uint16_t tag;
        uint32_t serial;
        std::shared_ptr<void> msg;  // 当isRaw为true时，msg中存的是std::string指针，当isRaw为false时，msg中存的是google::protobuf::Message指针。这是为了广播或转发消息给玩家时不需要对数据进行protobuf编解码
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
        NormalLink(): head_(nullptr), tail_(nullptr), eIter_(nullptr) {}
        ~NormalLink() {
            clear();
        }
        
        void push_back(Node *node);
        Node* pop_front();
        void erase(Node *node);
        void erase(const Iterator &iter);
        void eraseTo(Node *node);
        void moveToTail(Node *node);
        void clear();
        
        Node* getHead() { return head_; }
        Node* getTail() { return tail_; }
        
        Iterator begin() const {
            Iterator iter(head_);
            return iter;
        }

        const Iterator& end() const { return eIter_; }

    private:
        Node *head_;
        Node *tail_;

        Iterator eIter_;
    };

    template <typename T>
    void NormalLink<T>::push_back(Node *node) {
        if (head_) {
            assert(tail_ && !tail_->next);
            node->next = nullptr;
            node->prev = tail_;
            tail_->next = node;
            tail_ = node;
        } else {
            assert(!tail_);
            node->next = nullptr;
            node->prev = nullptr;
            head_ = node;
            tail_ = node;
        }
    }

    template <typename T>
    typename NormalLink<T>::Node* NormalLink<T>::pop_front() {
        if (!head_) {
            return nullptr;
        }
        
        Node *node = head_;
        head_ = head_->next;
        if (head_) {
            head_->prev = nullptr;
        } else {
            assert(tail_ == node);
            tail_ = nullptr;
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
        
        if (head_ == node) {
            head_ = next;
        }
        
        if (tail_ == node) {
            tail_ = prev;
        }
        
        delete node;
    }

    template <typename T>
    void NormalLink<T>::erase(const Iterator &iter) {
        erase(iter.ptr);
    }

    template <typename T>
    void NormalLink<T>::eraseTo(Node *node) {
        assert(node != nullptr);
        Node *tmpNode = head_;

        while (tmpNode != node) {
            assert(tmpNode != nullptr);
            Node *next = tmpNode->next;
            delete tmpNode;
            tmpNode = next;
        }

        head_ = node->next;
        if (head_) {
            head_->prev = nullptr;
        } else {
            assert(tail_ == node);
            tail_ = nullptr;
        }

        delete node;
    }

    template <typename T>
    void NormalLink<T>::moveToTail(Node *node) {
        Node *next = node->next;
        
        assert(next || tail_ == node);
        
        if (next) {
            Node *prev = node->prev;
            next->prev = prev;
            
            if (prev) {
                prev->next = next;
            }
            
            if (head_ == node) {
                head_ = next;
            }
            
            node->next = nullptr;
            node->prev = tail_;
            tail_->next = node;
            tail_ = node;
        }
    }

    template <typename T>
    void NormalLink<T>::clear() {
        Node *node = head_;
        while (node) {
            Node *next = node->next;
            delete node;
            node = next;
        }
        
        head_ = nullptr;
        tail_ = nullptr;
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
