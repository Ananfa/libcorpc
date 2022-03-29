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
    if (_running) {
        _running = false;

        _cond.broadcast();
    }
}

std::shared_ptr<Timer> Timer::create(uint32_t timeout_ms, const std::function<void()> &cb) {
    struct EnableMakeShared : public Timer {
        EnableMakeShared(uint32_t timeout_ms, const std::function<void()> &cb): Timer(timeout_ms, cb) {}
    };

    TimerPtr *timerPtr = new TimerPtr;
    timerPtr->timer = std::static_pointer_cast<Timer>(std::make_shared<EnableMakeShared>(timeout_ms, cb));

    corpc::RoutineEnvironment::startCoroutine(timerRoutine, timerPtr);
}

void *Timer::timerRoutine(void *arg) {
    TimerPtr *timerPtr = (TimerPtr *)arg;
    std::shared_ptr<Timer> timer = timerPtr->timer;
    delete timerPtr;

    if (timer->_timeout_ms > 0) {
        timer->_cond.wait(timer->_timeout_ms);
    }

    if (!timer->_running) {
        return nullptr;
    }

    timer->_cb();
    timer->_running = false;
    return nullptr;
}