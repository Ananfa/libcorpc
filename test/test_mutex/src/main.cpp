/*
 * Created by Xianke Liu on 2018/3/2.
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

#include <vector>
#include <thread>

using namespace corpc;

Mutex lk;
pthread_mutex_t myMutex = PTHREAD_MUTEX_INITIALIZER;
int threadNum = 4;
int routineNumPerThread = 2;
int totalRoutineNum = threadNum * routineNumPerThread;
//std::atomic<int> g_count;
std::vector<int> counts;

uint64_t maxDeltaTime = 0;

static void *log_routine( void *arg )
{
    pid_t pid = GetPid();
    stCoRoutine_t *co = co_self();
    //int lastCount = 0;

    std::vector<int> lastCounts;
    lastCounts.resize(totalRoutineNum);
    while (true) {
        sleep(1);
        
        //int count = g_count.load();
        int tps = 0;
        for (int i = 0; i < totalRoutineNum; i++) {
            int count = counts[i];
            int nps = count-lastCounts[i];
            if (nps > 0) {
                LOG("coId: %d, numPerSecond:%d\n", i, nps);
            }
            lastCounts[i] = count;
            tps += nps;
        }

        // 由于每次获取锁都会产生协程切换，同一时间只有一个协程获得锁，因此这里平均每秒的总数和单线程每秒切换协程的次数（瓶颈）相关
        LOG("total numPerSecond:%d maxDeltaTime:%llu\n", tps, maxDeltaTime);
    }
    
    return NULL;
}

static void *thread2_routine( void *arg )
{
    uint64_t coId = (uint64_t)arg;
    pid_t pid = GetPid();
    stCoRoutine_t *co = co_self();
    LOG("thread2_routine begin, pid:%d, co: %d\n", pid, co);
    int count = 0;
    struct timeval t;
    int64_t beforeTime = 0;
    while (true)
    {
        gettimeofday(&t, NULL);
        beforeTime = t.tv_sec * 1000000 + t.tv_usec;
        {
            LockGuard guard(lk);
            //pthread_mutex_lock(&myMutex);
            gettimeofday(&t, NULL);
            int64_t afterTime = t.tv_sec * 1000000 + t.tv_usec;
            int64_t delta = afterTime - beforeTime;

            if (delta > maxDeltaTime) {
                maxDeltaTime = delta;
            }

            //msleep(1);
            //LOG("thread2_routine doing, pid:%d, co: %d\n", pid, co);
            counts[coId]++;
            //int count = g_count.fetch_add(1);
            //if (count % 10000 == 0) {
            //    LOG("thread2_routine doing, pid:%d, co: %d, count: %d\n", pid, co, count);
            //}
            //pthread_mutex_unlock(&myMutex);
        }

        count++;
        if (count > 1000) {
            msleep(1);
            count = 0;
        }
    }

    LOG("thread2_routine end, pid:%d, co: %d\n", pid, co);
    
    return NULL;
}

void thread2(int id) {
    LOG("thread2 %d running...\n", GetPid());
    
    for (int i = 0; i < routineNumPerThread; i++) {
        uint64_t coId = id * routineNumPerThread + i;
        RoutineEnvironment::startCoroutine(thread2_routine, (void *)coId);
    }

    //RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop();
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    counts.resize(totalRoutineNum);
    //g_count = 0;
    RoutineEnvironment::startCoroutine(log_routine, NULL);

    std::vector<std::thread> threads;
    for (int i = 0; i < threadNum; i++) {
        threads.push_back(std::thread(thread2, i));
    }

    RoutineEnvironment::runEventLoop();
    
    return 0;
}
