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
    int retryTimes = 0;
    while (true) {
        int v = _res.load();
        if (v == 0) {
            // 尝试把_res值从0改成-1（防止此时锁被其他线程解锁，导致本协程进入不会被唤醒的等待）
            if (_res.compare_exchange_weak(v, -1)) {
                //assert(v == 0);
                // 若改成功，将本协程插入等待唤醒队列（由于只会有一个协程成功将_res改为-1，因此这里不需要用锁或者CAS机制），然后将_res从-1改为0（这里必然一次成功），yeld协程等待唤醒，退出
                _waitRoutines.push_back({GetPid(), co_self()});

                //assert(_res.load() == -1);

                v = -1;
                while (!_res.compare_exchange_weak(v, 0)) {
                    ERROR_LOG("Mutex::lock -- cant change _res from -1 to 0, v= %d\n", v);
                    v = -1;
                }

                co_yield_ct(); // 等待锁让出给当前协程时唤醒
                return;
            }

            // 若改不成功，跳回第1步（此时_lock的值是大于0或-1，这里需要切出协程，防止死循环占用CPU）
            retryTimes++;
            if (retryTimes >= 5) {
                msleep(1); // 让出协程，防止死循环占用CPU
                retryTimes = 0;
            }
        } else if (v > 0) {
            // 尝试把_lock值从1改成0
            if (_res.compare_exchange_weak(v, v-1)) {
                // 若改成功则获得锁，退出
                return;
            }
            // 若改不成功时，跳回第一步
        } else { // v == -1 || v == -2
            retryTimes++;
            if (retryTimes >= 5) {
                msleep(1); // 让出协程，防止死循环占用CPU
                retryTimes = 0;
            }
        }
    }
}

void Semaphore::post() {
    int retryTimes = 0;
    while (true) {
        int v = _res.load();
        if (v == 0) {
            // 尝试把_lock值从0改为-2
            if (_res.compare_exchange_weak(v, -2)) {
                // 若改成功，判断待唤醒队列是否有元素
                if (_waitRoutines.empty()) {
                    // 若没有元素，将_lock值从-2改为1，退出

                    //assert(_res.load() == -2);

                    v = -2;
                    while (!_res.compare_exchange_weak(v, 1)) {
                        ERROR_LOG("Semaphore::post -- cant change _res from -2 to 1, v = %d\n", v);
                        v = -2;
                    }
                } else {
                    // 若有元素，从待唤醒队列pop出头部元素，将_lock值从-2改为0，唤醒头部元素协程，退出
                    RoutineInfo info = _waitRoutines.front();
                    _waitRoutines.pop_front();

                    //assert(_res.load() == -2);

                    v = -2;
                    while (!_res.compare_exchange_weak(v, 0)) {
                        ERROR_LOG("Semaphore::post -- cant change _res from -2 to 0, v= %d\n", v);
                        v = -2;
                    }

                    RoutineEnvironment::resumeCoroutine(info.pid, info.co, 0);
                }
                return;
            }
        } else if (v > 0) {
            int u = v + 1;
            if (_res.compare_exchange_weak(v, -2)) {
                assert(_waitRoutines.empty()); // 这里应该不会有等待信号量的协程
                v = -2;
                while (!_res.compare_exchange_weak(v, u)) {
                    ERROR_LOG("Semaphore::post -- cant change _res from -2 to %d, v = %d\n", u, v);
                    v = -2;
                }
            }
        }

        // 若改不成功，跳回第1步（这里需要切出协程，防止死循环占用CPU）
        retryTimes++;
        if (retryTimes >= 5) {
            msleep(1); // 让出协程，防止死循环占用CPU
            retryTimes = 0;
        }
    }
}