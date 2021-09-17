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

#include "corpc_rand.h"
#include "corpc_utils.h"
#include "corpc_routine_env.h"

#include <vector>
#include <thread>

using namespace corpc;

Mutex lk;
int threadNum = 2;
int routineNumPerThread = 3;
int g_count = 0;

static void *log_routine( void *arg )
{
    pid_t pid = GetPid();
    stCoRoutine_t *co = co_self();
    while (true) {
        sleep(1);
        
        LOG("log in pid:%d, co: %d\n", pid, co);
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
        sleep(1);

        uint64_t r = randInt();
        LOG("pid:%d, co: %d, r: %llu\n", pid, co, r);
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
    
    std::vector<std::thread> threads;
    for (int i = 0; i < threadNum; i++) {
        threads.push_back(std::thread(thread2));
    }

    RoutineEnvironment::runEventLoop();
    
    return 0;
}
