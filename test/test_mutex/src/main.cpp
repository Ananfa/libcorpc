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
int threadNum = 5;
int routineNumPerThread = 100;
std::atomic<int> g_count;

static void *log_routine( void *arg )
{
    pid_t pid = GetPid();
    stCoRoutine_t *co = co_self();
    int lastCount = 0;
    while (true) {
        sleep(1);
        
        int count = g_count.load();
        LOG("log in pid:%d, co: %d, numPerSecond:%d\n", pid, co, count-lastCount);
        lastCount = count;
    }
    
    return NULL;
}

static void *thread2_routine( void *arg )
{
    pid_t pid = GetPid();
    stCoRoutine_t *co = co_self();
    LOG("thread2_routine begin, pid:%d, co: %d\n", pid, co);
    while (true)
    {
        LockGuard guard(lk);

        //msleep(1);
        //LOG("thread2_routine doing, pid:%d, co: %d\n", pid, co);
        int count = g_count.fetch_add(1);
        //if (count % 10000 == 0) {
        //    LOG("thread2_routine doing, pid:%d, co: %d, count: %d\n", pid, co, count);
        //}
    }

    LOG("thread2_routine end, pid:%d, co: %d\n", pid, co);
    
    return NULL;
}

void thread2() {
    LOG("thread2 %d running...\n", GetPid());
    
    for (int i = 0; i < routineNumPerThread; i++) {
        RoutineEnvironment::startCoroutine(thread2_routine, NULL);
    }

    //RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop();
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    //g_count = 0;
    RoutineEnvironment::startCoroutine(log_routine, NULL);

    std::vector<std::thread> threads;
    for (int i = 0; i < threadNum; i++) {
        threads.push_back(std::thread(thread2));
    }

    RoutineEnvironment::runEventLoop();
    
    return 0;
}
