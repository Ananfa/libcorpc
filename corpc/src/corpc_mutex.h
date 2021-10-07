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

namespace corpc {
    class Mutex {
        struct RoutineInfo {
            pid_t pid;
            stCoRoutine_t *co;
        };

    public:
        Mutex(): _lock(1) {}
        ~Mutex() {}
        
        void lock();
        void unlock();

    private:
        std::atomic<int> _lock;
        std::list<RoutineInfo> _waitRoutines;
    };

    class LockGuard {
    private:
        // 禁止在堆中创建对象
        void* operator new(size_t t) {}
        void operator delete(void* ptr) {}

    public:
        LockGuard(Mutex &lock): _lock(lock) { _lock.lock(); }
        ~LockGuard() { _lock.unlock(); }

    private:
        Mutex &_lock;
    };
}

#endif /* corpc_mutex_h */
