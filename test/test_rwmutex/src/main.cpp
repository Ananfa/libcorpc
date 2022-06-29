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

#include "corpc_rwmutex.h"
#include "corpc_utils.h"
#include "corpc_routine_env.h"

#include <vector>
#include <thread>

using namespace corpc;

RWMutex lk;
int syncData = 0;
int threadNum = 5;
int routineNumPerThread = 30;
std::atomic<int> g_count;

static void *write_routine( void *arg )
{
    pid_t pid = GetPid();
    stCoRoutine_t *co = co_self();
    while (true) {
        sleep(1);
        LOG("write_routine 1\n");
        lk.lock();
        LOG("write_routine 2\n");
        syncData++;
        lk.unlock();
        LOG("write_routine pid:%d, co: %d, data:%d\n", pid, co, syncData);
    }
    
    return NULL;
}

static void *read_routine( void *arg )
{
    pid_t pid = GetPid();
    stCoRoutine_t *co = co_self();
    LOG("read_routine begin, pid:%d, co: %d\n", pid, co);
    int lastData = syncData;
    while (true)
    {
        lk.rlock();

        int count = g_count.fetch_add(1);

        if (lastData != syncData) {
            lastData = syncData;
            //LOG("read_routine doing, pid:%d, co: %d, count: %d\n", pid, co, count);
        }
        msleep(1);

        lk.runlock();
    }

    LOG("read_routine end, pid:%d, co: %d\n", pid, co);
    
    return NULL;
}

void readThread() {
    LOG("readThread %d running...\n", GetPid());
    
    for (int i = 0; i < routineNumPerThread; i++) {
        RoutineEnvironment::startCoroutine(read_routine, NULL);
    }
    
    RoutineEnvironment::runEventLoop();
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    RoutineEnvironment::startCoroutine(write_routine, NULL);

    std::vector<std::thread> threads;
    for (int i = 0; i < threadNum; i++) {
        threads.push_back(std::thread(readThread));
    }

    RoutineEnvironment::runEventLoop();
    
    return 0;
}
