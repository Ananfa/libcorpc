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

#include "corpc_rwmutex.h"
#include "corpc_utils.h"

using namespace corpc;

void RWMutex::lock() {
    // 与其他获取写锁之间同步
    wlock_.lock();

    int r = readerCount_.fetch_sub(RWMUTEXMAXREADERS);
    if (r != 0) {
        int r1 = readerWait_.fetch_add(r);
        if (r1 + r != 0) {
            writerSem_.wait();
        }
    }
}

void RWMutex::unlock() {
    int r = readerCount_.fetch_add(RWMUTEXMAXREADERS);

    if (r >= 0) {
        ERROR_LOG("RWMutex::unlock -- Unlock of unlocked RWMutex\n");
        abort();
    }

    r += RWMUTEXMAXREADERS;

    for (int i = 0; i < r; i++) {
        readerSem_.post();
    }

    wlock_.unlock();
}

void RWMutex::rlock() {
    int r = readerCount_.fetch_add(1);

    if (r == -1 || r >= RWMUTEXMAXREADERS - 1) {
        ERROR_LOG("RWMutex::runlock -- rlock overflow\n");
        abort();
    }

    if (r < 0) {
        readerSem_.wait();
    }
}

void RWMutex::runlock() {
    int r = readerCount_.fetch_sub(1);
    if (r <= 0) {
        if (r == 0 || r == -RWMUTEXMAXREADERS) {
            ERROR_LOG("RWMutex::runlock -- RUnlock of unlocked RWMutex\n");
            abort();
        }

        // 有写锁在等待
        int r1 = readerWait_.fetch_sub(1);
        if (r1 == 1) {
            writerSem_.post();
        }
    }
}