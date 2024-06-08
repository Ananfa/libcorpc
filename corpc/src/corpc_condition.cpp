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

#include "corpc_condition.h"
#include "corpc_utils.h"
#include "corpc_routine_env.h"

using namespace corpc;

void Condition::wait(Mutex &lock) {
    while (true) {
        int v = res_.load();
        if (v == 0) {
            // 尝试把_res值从0改成1（防止此时条件变量被其他线程操作）
            if (res_.compare_exchange_weak(v, 1)) {
                // 在条件变量处挂起协程等待唤醒，挂起前需要先释放锁
                waitRoutines_.push_back({GetPid(), co_self()});

                res_.store(0);

                lock.unlock(); // unlock中没有协程切换

                co_yield_ct(); // 等待锁让出给当前协程时唤醒
                lock.lock(); // 重新获得锁

                return;
            }
        }

        // 自旋一会
        for (int i = 0; i < ACTIVE_SPIN_CNT; i++) {
            corpc_cpu_pause();
        }
    }
}

void Condition::signal() {
    // 唤醒一个等待协程
    while (true) {
        int v = res_.load();
        if (v == 0) {
            // 尝试把_res值从0改成1（防止此时条件变量被其他线程操作）
            if (res_.compare_exchange_weak(v, 1)) {
                if (waitRoutines_.empty()) {
                    // 没有可以唤醒的协程
                    res_.store(0);
                } else {
                    // 唤醒一个协程
                    RoutineInfo info = waitRoutines_.front();
                    waitRoutines_.pop_front();

                    res_.store(0);

                    RoutineEnvironment::resumeCoroutine(info.pid, info.co, 0);
                }

                return;
            }
        }

        // 自旋一会
        for (int i = 0; i < ACTIVE_SPIN_CNT; i++) {
            corpc_cpu_pause();
        }
    }
}

void Condition::broadcast() {
    // 唤醒所有等待协程
    while (true) {
        int v = res_.load();
        if (v == 0) {
            // 尝试把_res值从0改成1（防止此时条件变量被其他线程操作）
            if (res_.compare_exchange_weak(v, 1)) {
                if (waitRoutines_.empty()) {
                    // 没有可以唤醒的协程
                    res_.store(0);
                } else {
                    // 唤醒所有协程
                    std::list<RoutineInfo> waitRoutines = std::move(waitRoutines_);

                    res_.store(0);

                    for (auto& info : waitRoutines) {
                        RoutineEnvironment::resumeCoroutine(info.pid, info.co, 0);
                    }                    
                }

                return;
            }
        }

        // 自旋一会
        for (int i = 0; i < ACTIVE_SPIN_CNT; i++) {
            corpc_cpu_pause();
        }
    }
}