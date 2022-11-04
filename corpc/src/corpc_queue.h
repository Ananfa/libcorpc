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

#ifndef corpc_queue_h
#define corpc_queue_h

#include "co_routine.h"
#include "co_routine_inner.h"
#include "corpc_define.h"
#include "corpc_mutex.h"
#include "corpc_semaphore.h"

namespace corpc {

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

    // 跨线程多协程阻塞等待消息队列
    template <typename T>
    class MPMC_NoLockBlockQueue {
    public:
        MPMC_NoLockBlockQueue():_sem(0) {}
        ~MPMC_NoLockBlockQueue() {}

        void push(T& v) {
            {
                LockGuard lock(_queueMutex);
                _queue.push_back(v);
            }

            _sem.post();
        }

        void push(T&& v) {
            {
                LockGuard lock( _queueMutex );
                _queue.push_back(std::move(v));
            }
            
            _sem.post();
        }

        T pop() {
            _sem.wait();

            {
                LockGuard lock( _queueMutex );
                T ret = std::move(_queue.front());
                _queue.pop_front();

                return ret;
            }
        }

    private:
        Semaphore _sem;
        Mutex _queueMutex;
        std::list<T> _queue;
    };

}

#endif /* corpc_queue_h */
