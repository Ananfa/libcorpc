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

#include "corpc_semaphore.h"
#include "corpc_utils.h"
#include "corpc_routine_env.h"

using namespace corpc;

void Semaphore::wait() {
    // 判断是否能直接获得资源
    // _res值：为0时表示已上锁，大于0时表示未上锁，为-1时表示有协程正在进行排队，为-2时表示正在进行解锁
    pid_t pid = GetPid();
    stCoRoutine_t *coSelf = co_self();
    //int retryTimes = 0;
    while (true) {
        int v = res_.load();
        if (v == 0) {
            // 尝试把_res值从0改成-1（防止此时锁被其他线程解锁，导致本协程进入不会被唤醒的等待）
            if (res_.compare_exchange_weak(v, -1)) {
                //assert(v == 0);
                // 若改成功，将本协程插入等待唤醒队列（由于只会有一个协程成功将_res改为-1，因此这里不需要用锁或者CAS机制），然后将_res从-1改为0（这里必然一次成功），yeld协程等待唤醒，退出
                waitRoutines_.push_back({pid, coSelf});

                //assert(res_.load() == -1);

                res_.store(0);

                co_yield_ct(); // 等待锁让出给当前协程时唤醒
                return;
            }

            // 自旋一会
            for (int i = 0; i < ACTIVE_SPIN_CNT; i++) {
                corpc_cpu_pause();
            }
        } else if (v > 0) {
            if (res_.compare_exchange_weak(v, v-1)) {
                // 若改成功则获得信号量，退出
                return;
            }
            // 若改不成功时，跳回第一步
        } else { // v == -1
            // 自旋一会
            for (int i = 0; i < ACTIVE_SPIN_CNT; i++) {
                corpc_cpu_pause();
            }
        }
    }
}

void Semaphore::post() {
    //int retryTimes = 0;
    while (true) {
        int v = res_.load();
        if (v == 0) {
            // 尝试把_lock值从0改为-1
            if (res_.compare_exchange_weak(v, -1)) {
                // 若改成功，判断待唤醒队列是否有元素
                if (waitRoutines_.empty()) {
                    // 若没有元素，将_lock值从-2改为1，退出
                    res_.store(1);
                } else {
                    // 若有元素，从待唤醒队列pop出头部元素，将_lock值从-2改为0，唤醒头部元素协程，退出
                    RoutineInfo info = waitRoutines_.front();
                    waitRoutines_.pop_front();

                    res_.store(0);

                    RoutineEnvironment::resumeCoroutine(info.pid, info.co, 0);
                }
                return;
            }

            // 自旋一会
            for (int i = 0; i < ACTIVE_SPIN_CNT; i++) {
                corpc_cpu_pause();
            }
        } else if (v > 0) {
            if (res_.compare_exchange_weak(v, v+1)) {
                // 若改成功则返还信号量，退出
                return;
            }
        } else {
            // 自旋一会
            for (int i = 0; i < ACTIVE_SPIN_CNT; i++) {
                corpc_cpu_pause();
            }
        }
    }
}