/*
 * Created by Xianke Liu on 2021/6/22.
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

#ifndef corpc_mutex_h
#define corpc_mutex_h

#include "co_routine.h"
#include <list>
#include <atomic>

#define MUTEXLOCKED 1
#define MUTEXWOKEN 2
#define MUTEXSTARVING 4
#define MUTEXWAITERSHIFT 3
#define STARVATIONTHRESHOLDNS 1000
#define ACTIVE_SPIN_CNT 30

namespace corpc {
    // 本实现参考go的Mutex实现（不可重入）
    class Mutex {
        struct RoutineInfo {
            pid_t pid;
            stCoRoutine_t *co;
        };

    public:
        Mutex(): state_(0), waitlock_(false), dontWait_(false) {}
        ~Mutex() {}
        
        void lock();
        void unlock();

    private:
        void lockSlow();
        void unlockSlow(int32_t newV);

        void wait(bool queueLifo);
        void post();

    private:
        std::atomic<int32_t> state_;
        std::atomic<bool> waitlock_;
        bool dontWait_;
        std::list<RoutineInfo> waitRoutines_;

        // 以下成员用于当有其他协程长时间等待锁无法获得时不抢锁
        stCoRoutine_t *lastco_ = nullptr; // 最近一次获取锁的协程
        int64_t beginTm_ = 0; // MUTEXWOKEN被设置时上面协程连续获得该锁的最早时间
    };

    class LockGuard {
    private:
        // 禁止在堆中创建对象
        void* operator new(size_t t) { throw std::bad_alloc(); }
        void operator delete(void* ptr) {}

    public:
        LockGuard(Mutex &lock): lock_(lock) { lock_.lock(); }
        ~LockGuard() { lock_.unlock(); }

    private:
        Mutex &lock_;
    };
}

#endif /* corpc_mutex_h */
