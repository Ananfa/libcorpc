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
#include "corpc_routine_env.h"

using namespace corpc;

void Mutex::lock() {
    int32_t v = 0;
    if (state_.compare_exchange_weak(v, MUTEXLOCKED)) {
        return;
    }

    lockSlow();
}

void Mutex::lockSlow() {
    stCoRoutine_t *coSelf = co_self();

    struct timeval t;
    int64_t waitStartTime = 0;
    bool starving = false;
    bool awoke = false;
    int iter = 0;

    int32_t oldV = state_.load();
    while (true) {
        // Don't spin in starvation mode, ownership is handed off to waiters
        // so we won't be able to acquire the mutex anyway.
        if ((oldV & (MUTEXLOCKED | MUTEXSTARVING)) == MUTEXLOCKED && iter < 4) {
            // Active spinning makes sense.
            // Try to set mutexWoken flag to inform Unlock
            // to not wake other blocked goroutines.
            if (!awoke && (oldV & MUTEXWOKEN == 0) && (oldV >> MUTEXWAITERSHIFT != 0) &&
                state_.compare_exchange_weak(oldV, oldV | MUTEXWOKEN)) {
                awoke = true;
            }

            // 自旋一会
            for (int i = 0; i < ACTIVE_SPIN_CNT; i++) {
                corpc_cpu_pause();
            }

            iter++;
            oldV = state_.load();
            continue;
        }

        // 判断当前是否有长时间等待的MUTEXWOKEN协程，防止同线程的协程得不到运行
        if (!awoke && (oldV & MUTEXWOKEN) != 0) {
            if (lastco_ == coSelf) {
                gettimeofday(&t, NULL);
                int64_t curTime = t.tv_sec * 1000000 + t.tv_usec;
                if ((curTime - beginTm_) > 1000) {
                    RoutineEnvironment::pause();
                    continue;
                }
            }
        }

        int32_t newV = oldV;
        // Don't try to acquire starving mutex, new arriving goroutines must queue.
        if ((oldV & MUTEXSTARVING) == 0) {
            newV |= MUTEXLOCKED;
        }
        if ((oldV & (MUTEXLOCKED | MUTEXSTARVING)) != 0) {
            newV += 1 << MUTEXWAITERSHIFT;
        }
        // The current goroutine switches mutex to starvation mode.
        // But if the mutex is currently unlocked, don't do the switch.
        // Unlock expects that starving mutex has waiters, which will not
        // be true in this case.
        if (starving && (oldV & MUTEXLOCKED) != 0) {
            newV |= MUTEXSTARVING;
        }
        if (awoke) {
            // The goroutine has been woken from sleep,
            // so we need to reset the flag in either case.
            if ((newV & MUTEXWOKEN) == 0) {
                ERROR_LOG("Mutex::lock -- inconsistent mutex state\n");
                abort();
            }
            newV &= ~MUTEXWOKEN;
        }
        if (state_.compare_exchange_weak(oldV, newV)) {
            if ((oldV & (MUTEXLOCKED | MUTEXSTARVING)) == 0) {
                if (lastco_ == nullptr) {
                    if ((oldV & MUTEXWOKEN) != 0) {
                        lastco_ = coSelf;
                        gettimeofday(&t, NULL);
                        beginTm_ = t.tv_sec * 1000000 + t.tv_usec;
                    }
                } else if (lastco_ != coSelf) {
                    lastco_ = nullptr;
                    beginTm_ = 0;
                }

                break; // locked the mutex with CAS
            }
            // If we were already waiting before, queue at the front of the queue.
            bool queueLifo = waitStartTime != 0;
            if (waitStartTime == 0) {
                gettimeofday(&t, NULL);
                waitStartTime = t.tv_sec * 1000000 + t.tv_usec;
            }

            wait(queueLifo);

            if (!starving) {
                gettimeofday(&t, NULL);
                int64_t curTime = t.tv_sec * 1000000 + t.tv_usec;
                starving = (curTime - waitStartTime) > 1000;
            }
            
            oldV = state_.load();

            if ((oldV & MUTEXSTARVING) != 0) {
                // If this goroutine was woken and mutex is in starvation mode,
                // ownership was handed off to us but mutex is in somewhat
                // inconsistent state: mutexLocked is not set and we are still
                // accounted as waiter. Fix that.
                if ((oldV & (MUTEXLOCKED | MUTEXWOKEN)) != 0 || (oldV >> MUTEXWAITERSHIFT) == 0) {
                    ERROR_LOG("Mutex::lock -- inconsistent mutex state\n");
                    abort();
                }

                int32_t delta = int32_t(MUTEXLOCKED - (1 << MUTEXWAITERSHIFT));
                if (!starving || (oldV >> MUTEXWAITERSHIFT) == 1) {
                    // Exit starvation mode.
                    // Critical to do it here and consider wait time.
                    // Starvation mode is so inefficient, that two goroutines
                    // can go lock-step infinitely once they switch mutex
                    // to starvation mode.
                    delta -= MUTEXSTARVING;
                }

                state_.fetch_add(delta);
                
                if (lastco_ != nullptr) {
                    lastco_ = nullptr;
                    beginTm_ = 0;
                }

                break;
            }
            awoke = true;
            iter = 0;
        } else {
            oldV = state_.load();
        }
    }
}

void Mutex::unlock() {
    // Fast path: drop lock bit.
    int32_t newV = state_.fetch_add(-MUTEXLOCKED) - MUTEXLOCKED;
    if (newV != 0) {
        // Outlined slow path to allow inlining the fast path.
        // To hide unlockSlow during tracing we skip one extra frame when tracing GoUnblock.
        unlockSlow(newV);
    }
}

void Mutex::unlockSlow(int32_t newV) {
    if (((newV + MUTEXLOCKED) & MUTEXLOCKED) == 0) {
        ERROR_LOG("Mutex::unlock -- unlock of unlocked mutex\n");
        abort();
    }

    if ((newV & MUTEXSTARVING) == 0) {
        int32_t oldV = newV;
        while (true) {
            // If there are no waiters or a goroutine has already
            // been woken or grabbed the lock, no need to wake anyone.
            // In starvation mode ownership is directly handed off from unlocking
            // goroutine to the next waiter. We are not part of this chain,
            // since we did not observe mutexStarving when we unlocked the mutex above.
            // So get off the way.
            if ((oldV >> MUTEXWAITERSHIFT) == 0 || (oldV & (MUTEXLOCKED|MUTEXWOKEN|MUTEXSTARVING)) != 0) {
                return;
            }
            // Grab the right to wake someone.
            newV = (oldV - (1<<MUTEXWAITERSHIFT)) | MUTEXWOKEN;

            if (state_.compare_exchange_weak(oldV, newV)) {
                post();
                return;
            }

            oldV = state_.load();
        }
    } else {
        // Starving mode: handoff mutex ownership to the next waiter, and yield
        // our time slice so that the next waiter can start to run immediately.
        // Note: mutexLocked is not set, the waiter will set it after wakeup.
        // But mutex is still considered locked if mutexStarving is set,
        // so new coming goroutines won't acquire it.
        post();
    }
}

void Mutex::wait(bool queueLifo) {
    pid_t pid = GetPid();
    stCoRoutine_t *coSelf = co_self();

    bool v = false;
    while (true) {
        v = false;
        if (waitlock_.compare_exchange_weak(v, true)) {
            if (dontWait_) {
//                ERROR_LOG("Mutex::wait -- dont wait pid:%d co:%d\n", pid, coSelf);
                dontWait_ = false;
                waitlock_.store(false);
                return;
            }

            if (queueLifo) {
                waitRoutines_.push_front({pid, coSelf});
            } else {
                waitRoutines_.push_back({pid, coSelf});
            }
            
            waitlock_.store(false);

            co_yield_ct(); // 等待锁让出给当前协程时唤醒
            return;
        } else {
            // 自旋一会
            for (int i = 0; i < ACTIVE_SPIN_CNT; i++) {
                corpc_cpu_pause();
            }
        }
    }
}

void Mutex::post() {
    while (true) {
        bool v = false;
        if (waitlock_.compare_exchange_weak(v, true)) {
            if (waitRoutines_.empty()) {
                dontWait_ = true;
                waitlock_.store(false);
                return;
            }

            RoutineInfo info = waitRoutines_.front();
            waitRoutines_.pop_front();

            waitlock_.store(false);

            RoutineEnvironment::resumeCoroutine(info.pid, info.co, 0);
            return;
        } else {
            // 自旋一会
            for (int i = 0; i < ACTIVE_SPIN_CNT; i++) {
                corpc_cpu_pause();
            }
        }
    }
}

