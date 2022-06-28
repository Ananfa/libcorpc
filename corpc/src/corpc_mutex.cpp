/*
 * Created by Xianke Liu on 2021/6/22.
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

#include "corpc_mutex.h"
#include "corpc_utils.h"
#include "corpc_routine_env.h"

using namespace corpc;

void Mutex::lock() {
    // 判断是否能直接获得锁
    // _lock值：为0时表示已上锁，为1时表示未上锁，为2时表示有协程正在排队，为3时表示正在解锁
    int retryTimes = 0;
    while (true) {
        int v = _lock.load();
        switch (v) {
            case 0: {
                // 尝试把_lock值从0改成2（防止此时锁被其他线程解锁，导致本协程进入不会被唤醒的等待）
                if (_lock.compare_exchange_weak(v, 2)) {
                    //assert(v == 0);
                    stCoRoutine_t *coSelf = co_self();
                    // 若改成功，将本协程插入等待唤醒队列（由于只会有一个协程成功将_lock改为2，因此这里不需要用锁或者CAS机制），然后将_lock从2改为0（这里必然一次成功），yeld协程等待唤醒，退出
                    _waitRoutines.push_back({GetPid(), coSelf});

                    //assert(_lock.load() == 2);

                    v = 2;
                    while (!_lock.compare_exchange_weak(v, 0)) {
                        ERROR_LOG("Mutex::lock -- cant change _lock from 2 to 0, v= %d\n", v);
                        v = 2;
                    }

                    co_yield_ct(); // 等待锁让出给当前协程时唤醒
                    _owner = coSelf; // 设置拥有者
                    return;
                }

                // 若改不成功，跳回第1步（此时_lock的值是1或2，这里需要切出协程，防止死循环占用CPU）
                retryTimes++;
                if (retryTimes >= 5) {
                    msleep(1); // 让出协程，防止死循环占用CPU
                    retryTimes = 0;
                }
                break;
            }
            case 1: {
                // 尝试把_lock值从1改成0
                if (_lock.compare_exchange_weak(v, 0)) {
                    // 若改成功则获得锁，退出
                    _owner = co_self(); // 记录锁的拥有者协程，释放时必须是拥有者协程才能释放
                    return;
                }
                // 若改不成功时，跳回第一步
                break;
            }
            default: {
                retryTimes++;
                if (retryTimes >= 5) {
                    msleep(1); // 让出协程，防止死循环占用CPU
                    retryTimes = 0;
                }
                break;
            }
        }
    }
}

void Mutex::unlock() {
    // 释放锁，只能由获得锁的协程来释放
    if (!_owner || _owner != co_self()) {
        ERROR_LOG("Mutex::unlock -- cant unlock for not owner\n");
        return;
    }

    int retryTimes = 0;
    while (true) {
        int v = _lock.load();
        if (v == 0) {
            // 尝试把_lock值从0改为3
            if (_lock.compare_exchange_weak(v, 3)) {
                // 若改成功，清除拥有者，并判断待唤醒队列是否有元素
                _owner = nullptr;
                if (_waitRoutines.empty()) {
                    // 若没有元素，将_lock值从3改为1，退出

                    //assert(_lock.load() == 3);

                    v = 3;
                    while (!_lock.compare_exchange_weak(v, 1)) {
                        ERROR_LOG("Mutex::unlock -- cant change _lock from 3 to 1, v = %d\n", v);
                        v = 3;
                    }
                } else {
                    // 若有元素，从待唤醒队列pop出头部元素，将_lock值从3改为0，唤醒头部元素协程，退出
                    RoutineInfo info = _waitRoutines.front();
                    _waitRoutines.pop_front();

                    //assert(_lock.load() == 3);

                    v = 3;
                    while (!_lock.compare_exchange_weak(v, 0)) {
                        ERROR_LOG("Mutex::unlock -- cant change _lock from 3 to 0, v= %d\n", v);
                        v = 3;
                    }

                    RoutineEnvironment::resumeCoroutine(info.pid, info.co, 0);
                }
                return;
            }
        }

        // 若改不成功，跳回第1步（此时_lock的值一定是2，这里需要切出协程，防止死循环占用CPU）
        assert(v == 2);
        retryTimes++;
        if (retryTimes >= 5) {
            msleep(1); // 让出协程，防止死循环占用CPU
            retryTimes = 0;
        }
    }
}