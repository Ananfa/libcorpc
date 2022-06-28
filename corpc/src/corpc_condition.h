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

#ifndef corpc_condition_h
#define corpc_condition_h

#include "corpc_mutex.h"

// 注意：此条件变量的实现可能跨线程使用
namespace corpc {
    class Condition {
        struct RoutineInfo {
            pid_t pid;
            stCoRoutine_t *co;
        };

    public:
        Condition(): _res(0) {}
        ~Condition() {}
        
        void wait(Mutex &lock);
        void signal();
        void broadcast();

    private:
    	std::atomic<int> _res;
        std::list<RoutineInfo> _waitRoutines;
    };
}

#endif /* corpc_condition_h */
