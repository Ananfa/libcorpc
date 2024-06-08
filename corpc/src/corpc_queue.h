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
        MPSC_NoLockQueue():head_(NULL), outqueue_(NULL) {}
        ~MPSC_NoLockQueue() {}
        
        void push(T& v) {
            Node *newNode = new Node;
            newNode->value = v;
            
            newNode->next = head_;
            while (!head_.compare_exchange_weak(newNode->next, newNode));
        }
        
        void push(T&& v) {
            Node *newNode = new Node;
            newNode->value = std::move(v);
            
            newNode->next = head_;
            while (!head_.compare_exchange_weak(newNode->next, newNode));
        }
        
        T pop() {
            T ret(nullptr);
            
            if (!outqueue_) {
                if (head_ != NULL) {
                    outqueue_ = head_;
                    while (!head_.compare_exchange_weak(outqueue_, NULL));
                    
                    // 翻转
                    Node *n1 = outqueue_;
                    outqueue_ = NULL;
                    while (n1) {
                        Node *n2 = n1->next;
                        n1->next = outqueue_;
                        outqueue_ = n1;
                        n1 = n2;
                    }
                }
            }
            
            if (outqueue_) {
                ret = std::move(outqueue_->value);
                Node *tnode = outqueue_;
                outqueue_ = outqueue_->next;
                delete tnode;
            }
            
            return ret;
        }
            
    private:
        std::atomic<Node*> head_;
        Node *outqueue_;
    };
    
    template <typename T>
    class Co_MPSC_NoLockQueue: public MPSC_NoLockQueue<T> {
    public:
        Co_MPSC_NoLockQueue() {
            pipe(queuePipe_.pipefd);
            
            co_register_fd(queuePipe_.pipefd[1]);
            co_set_nonblock(queuePipe_.pipefd[1]);
        }
        ~Co_MPSC_NoLockQueue() {
            close(queuePipe_.pipefd[0]);
            close(queuePipe_.pipefd[1]);
        }
        
        int getReadFd() { return queuePipe_.pipefd[0]; }
        int getWriteFd() { return queuePipe_.pipefd[1]; }
        
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
        PipeType queuePipe_; // 管道（用于通知处理协程有新rpc任务入队）
    };

    // 另一种形式的非锁实现
    // 注意：该Queue实现只支持多生产者和单消费者情形（因为_outqueue不是线程安全）
    template <typename T>
    class SyncQueue {
    public:
        SyncQueue() {}
        ~SyncQueue() {}
        
        void push(T & v) {
            LockGuard lock( queueMutex_ );
            inqueue_.push_back(v);
        }
        
        void push(T && v) {
            LockGuard lock( queueMutex_ );
            inqueue_.push_back(std::move(v));
        }
        
        T pop() {
            T ret(nullptr);
            
            if (!outqueue_.empty()) {
                ret = std::move(outqueue_.front());
                
                outqueue_.pop_front();
            } else {
                if (!inqueue_.empty()) {
                    {
                        LockGuard lock( queueMutex_ );
                        inqueue_.swap(outqueue_);
                    }
                    
                    ret = std::move(outqueue_.front());
                    
                    outqueue_.pop_front();
                }
            }
            
            return ret;
        }
        
    private:
        Mutex queueMutex_;
        std::list<T> inqueue_;
        std::list<T> outqueue_;
    };
    
    template <typename T>
    class CoSyncQueue: public SyncQueue<T> {
    public:
        CoSyncQueue() {
            pipe(queuePipe_.pipefd);
            
            co_register_fd(queuePipe_.pipefd[1]);
            co_set_nonblock(queuePipe_.pipefd[1]);
        }
        ~CoSyncQueue() {
            close(queuePipe_.pipefd[0]);
            close(queuePipe_.pipefd[1]);
        }
        
        int getReadFd() { return queuePipe_.pipefd[0]; }
        int getWriteFd() { return queuePipe_.pipefd[1]; }
        
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
        PipeType queuePipe_; // 管道（用于通知处理协程有新rpc任务入队）
    };
    
     // 注意：该Queue实现只支持多生产者和单消费者情形
    template <typename T>
    class LockQueue {
    public:
        LockQueue() {}
        ~LockQueue() {}
        
        void push(T & v) {
            std::unique_lock<std::mutex> lock( queueMutex_ );
            inqueue_.push_back(v);
        }
        
        void push(T && v) {
            std::unique_lock<std::mutex> lock( queueMutex_ );
            inqueue_.push_back(std::move(v));
        }
        
        T pop() {
            T ret(nullptr);
            
            if (!outqueue_.empty()) {
                ret = std::move(outqueue_.front());
                
                outqueue_.pop_front();
            } else {
                if (!inqueue_.empty()) {
                    {
                        std::unique_lock<std::mutex> lock( queueMutex_ );
                        inqueue_.swap(outqueue_);
                    }
                    
                    ret = std::move(outqueue_.front());
                    
                    outqueue_.pop_front();
                }
            }
            
            return ret;
        }
        
    private:
        std::mutex queueMutex_;
        std::list<T> inqueue_;
        std::list<T> outqueue_;
    };
    
    template <typename T>
    class CoLockQueue: public LockQueue<T> {
    public:
        CoLockQueue() {
            pipe(queuePipe_.pipefd);
            
            co_register_fd(queuePipe_.pipefd[1]);
            co_set_nonblock(queuePipe_.pipefd[1]);
        }
        ~CoLockQueue() {
            close(queuePipe_.pipefd[0]);
            close(queuePipe_.pipefd[1]);
        }
        
        int getReadFd() { return queuePipe_.pipefd[0]; }
        int getWriteFd() { return queuePipe_.pipefd[1]; }
        
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
        PipeType queuePipe_; // 管道（用于通知处理协程有新rpc任务入队）
    };

    // 跨线程多协程阻塞等待消息队列
    template <typename T>
    class MPMC_NoLockBlockQueue {
    public:
        MPMC_NoLockBlockQueue():sem_(0) {}
        ~MPMC_NoLockBlockQueue() {}

        void push(T& v) {
            {
                LockGuard lock(queueMutex_);
                queue_.push_back(v);
            }

            sem_.post();
        }

        void push(T&& v) {
            {
                LockGuard lock( queueMutex_ );
                queue_.push_back(std::move(v));
            }
            
            sem_.post();
        }

        T pop() {
            sem_.wait();

            {
                LockGuard lock( queueMutex_ );
                T ret = std::move(queue_.front());
                queue_.pop_front();

                return ret;
            }
        }

    private:
        Semaphore sem_;
        Mutex queueMutex_;
        std::list<T> queue_;
    };

}

#endif /* corpc_queue_h */
