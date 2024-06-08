/*
 * Created by Xianke Liu on 2021/3/29.
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

#include "corpc_timer.h"
#include "corpc_routine_env.h"

using namespace corpc;

void Timer::stop() {
    if (running_) {
        running_ = false;

        cond_.broadcast();
    }
}

std::shared_ptr<Timer> Timer::create(uint32_t timeout_ms, const std::function<void()> &cb) {
    struct EnableMakeShared : public Timer {
        EnableMakeShared(uint32_t timeout_ms, const std::function<void()> &cb): Timer(timeout_ms, cb) {}
    };

    std::shared_ptr<Timer> timer = std::static_pointer_cast<Timer>(std::make_shared<EnableMakeShared>(timeout_ms, cb));
    
    TimerRoutineArg *arg = new TimerRoutineArg;
    arg->timer = timer;
    corpc::RoutineEnvironment::startCoroutine(timerRoutine, arg);

    return timer;
}

void *Timer::timerRoutine(void *arg) {
    TimerRoutineArg *routineArg = (TimerRoutineArg *)arg;
    std::shared_ptr<Timer> timer = routineArg->timer;
    delete routineArg;

    if (timer->timeout_ms_ > 0) {
        timer->cond_.wait(timer->timeout_ms_);
    }

    if (!timer->running_) {
        return nullptr;
    }

    timer->cb_();
    timer->running_ = false;
    return nullptr;
}