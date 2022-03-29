/*
 * Created by Xianke Liu on 2022/3/29.
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

#ifndef corpc_timer_h
#define corpc_timer_h

#include "co_routine.h"
#include "corpc_cond.h"
#include <memory>
#include <functional>

// 注意：此条件计时器的实现不能跨线程使用
namespace corpc {
    class Timer {
        struct TimerPtr {
            std::shared_ptr<Timer> timer;
        };

    public:
        static std::shared_ptr<Timer> create(uint32_t timeout_ms, const std::function<void()> &cb);

        static void *timerRoutine(void *arg);

        void stop();
    private:
        Timer(uint32_t timeout_ms, const std::function<void()> &cb): _timeout_ms(timeout_ms), _cb(cb) {}
        ~Timer() {}

    private:
        Cond _cond;

        uint32_t _timeout_ms;
        std::function<void()> _cb;

        bool _running = true;
    };
}

#endif /* corpc_timer_h */
