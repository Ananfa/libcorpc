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

#include "corpc_utils.h"
#include "corpc_routine_env.h"

#include <vector>
#include <thread>

using namespace corpc;

bool g_start = false;
pid_t g_pid = 0;
stCoRoutine_t *g_co = nullptr;
uint64_t g_count = 0;

static void *log_routine( void *arg )
{
    //std::vector<int> lastCounts;
    //lastCounts.resize(totalRoutineNum);
    int lastCount = g_count;
    while (true) {
        sleep(1);
        
        //int tps = 0;
        //for (int i = 0; i < totalRoutineNum; i++) {
        //    int count = counts[i];
        //    int nps = count-lastCounts[i];
        //    LOG("coId: %d, numPerSecond:%d\n", i, nps);
        //    lastCounts[i] = count;
        //    tps += nps;
        //}

        // 由于每次获取锁都会产生协程切换，同一时间只有一个协程获得锁，因此这里平均每秒的总数和单线程每秒切换协程的次数（瓶颈）相关
        int count = g_count;
        LOG("total numPerSecond:%d\n", count - lastCount);
        lastCount = count;
    }
    
    return NULL;
}

static void *thread2_routine( void *arg )
{
    uint64_t sleepTm = (uint64_t)arg;
    pid_t pid = GetPid();
    stCoRoutine_t *co = co_self();
    LOG("thread2_routine begin, pid:%d, co: %d\n", pid, co);

    if (sleepTm > 0) {
        msleep(sleepTm);
    }
    
    while (true)
    {
        pid_t old_pid = g_pid;
        stCoRoutine_t *old_co = g_co;

        g_pid = pid;
        g_co = co;

        if (old_co) {
            RoutineEnvironment::resumeCoroutine(old_pid, old_co, 0);
        }

        co_yield_ct();
        g_count++;
    }

    return NULL;
}

void thread2(uint64_t sleepTm) {
    LOG("thread2 %d running...\n", GetPid());
    
    RoutineEnvironment::startCoroutine(thread2_routine, (void *)sleepTm);

    //RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop();
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    RoutineEnvironment::startCoroutine(log_routine, NULL);

    // TODO: 测试跨线程的协程切换
    //RoutineEnvironment::startCoroutine(thread2_routine, NULL);
    //RoutineEnvironment::startCoroutine(thread2_routine, NULL);

    std::thread t1 = std::thread(thread2, 0);
    std::thread t2 = std::thread(thread2, 1000);

    //std::vector<std::thread> threads;
    //for (int i = 0; i < threadNum; i++) {
    //    threads.push_back(std::thread(thread2, i));
    //}

    RoutineEnvironment::runEventLoop();
    
    return 0;
}
