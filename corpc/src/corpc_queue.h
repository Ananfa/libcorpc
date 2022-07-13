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
#include "corpc_mutex.h"
#include "corpc_semaphore.h"

namespace corpc {

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
