/*
 * Created by Xianke Liu on 2021/7/22.
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

#ifndef corpc_semaphore_h
#define corpc_semaphore_h

#include "co_routine.h"
#include <list>
#include <atomic>

namespace corpc {
    class Semaphore {
        struct RoutineInfo {
            pid_t pid;
            stCoRoutine_t *co;
        };

    public:
        Semaphore(int num): _res(num) {}
        ~Semaphore() {}
        
        void wait();
        void post();

    private:
        std::atomic<int> _res;
        std::list<RoutineInfo> _waitRoutines;
    };

}

#endif /* corpc_semaphore_h */
