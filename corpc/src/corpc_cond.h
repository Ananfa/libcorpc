/*
 * Created by Xianke Liu on 2021/8/11.
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

#ifndef corpc_cond_h
#define corpc_cond_h

#include "co_routine.h"

// 注意：此条件变量的实现不能跨线程使用，只能同线程内协程间使用
namespace corpc {
    class Cond {
    public:
        Cond();
        ~Cond();
        
        void signal();
        void broadcast();
        void wait(int timeout); // 参数单位毫秒

    private:
        stCoCond_t* cond_;
    };
}

#endif /* corpc_cond_h */
