/*
 * Created by Xianke Liu on 2022/6/28.
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

#ifndef corpc_rwmutex_h
#define corpc_rwmutex_h

#include "corpc_mutex.h"
#include "corpc_semaphore.h"

namespace corpc {
    const int RWMUTEXMAXREADERS = 1 << 30;

    // 读写锁(参考go的读写锁实现，不可重入)
    class RWMutex {

    public:
        RWMutex():_readerSem(0), _writerSem(0), _readerCount(0), _readerWait(0) {}
        ~RWMutex() {}
        
        void lock();
        void unlock();

        void rlock();
        void runlock();

    private:
        Mutex _wlock;
        Semaphore _readerSem;
        Semaphore _writerSem;
        std::atomic<int> _readerCount;
        std::atomic<int> _readerWait;
    };

    class RLockGuard {
    private:
        // 禁止在堆中创建对象
        void* operator new(size_t t) {}
        void operator delete(void* ptr) {}

    public:
        RLockGuard(RWMutex &lock): _lock(lock) { _lock.rlock(); }
        ~RLockGuard() { _lock.runlock(); }

    private:
        RWMutex &_lock;
    };

    class WLockGuard {
    private:
        // 禁止在堆中创建对象
        void* operator new(size_t t) {}
        void operator delete(void* ptr) {}

    public:
        WLockGuard(RWMutex &lock): _lock(lock) { _lock.lock(); }
        ~WLockGuard() { _lock.unlock(); }

    private:
        RWMutex &_lock;
    };
}

#endif /* corpc_mutex_h */
