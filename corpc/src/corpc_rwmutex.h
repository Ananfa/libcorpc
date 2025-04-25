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
        RWMutex():readerSem_(0), writerSem_(0), readerCount_(0), readerWait_(0) {}
        ~RWMutex() {}
        
        void lock();
        void unlock();

        void rlock();
        void runlock();

    private:
        Mutex wlock_;
        Semaphore readerSem_;
        Semaphore writerSem_;
        std::atomic<int> readerCount_;
        std::atomic<int> readerWait_;
    };

    class RLockGuard {
    private:
        // 禁止在堆中创建对象
        void* operator new(size_t t) { throw std::bad_alloc(); }
        void operator delete(void* ptr) {}

    public:
        RLockGuard(RWMutex &lock): lock_(lock) { lock_.rlock(); }
        ~RLockGuard() { lock_.runlock(); }

    private:
        RWMutex &lock_;
    };

    class WLockGuard {
    private:
        // 禁止在堆中创建对象
        void* operator new(size_t t) { throw std::bad_alloc(); }
        void operator delete(void* ptr) {}

    public:
        WLockGuard(RWMutex &lock): lock_(lock) { lock_.lock(); }
        ~WLockGuard() { lock_.unlock(); }

    private:
        RWMutex &lock_;
    };
}

#endif /* corpc_mutex_h */
