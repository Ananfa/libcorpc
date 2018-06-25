/*
 * Created by Xianke Liu on 2018/5/15.
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

#include "corpc_memcached.h"
#include "corpc_utils.h"

#include <thread>

using namespace corpc;

static std::atomic<int> g_cnt(0);

static void *log_routine( void *arg )
{
    co_enable_hook_sys();
    
    int total = 0;
    int average = 0;
    
    time_t startAt = time(NULL);
    
    while (true) {
        sleep(1);
        
        total += g_cnt;
        
        if (total == 0) {
            startAt = time(NULL);
            continue;
        }
        
        time_t now = time(NULL);
        
        time_t difTime = now - startAt;
        if (difTime > 0) {
            average = total / difTime;
        } else {
            average = total;
        }
        
        printf("time %ld seconds, cnt: %d, average: %d, total: %d\n", difTime, int(g_cnt), average, total);
        
        g_cnt = 0;
    }
    
    return NULL;
}

static void *memc_routine( void *arg )
{
    co_enable_hook_sys();
    
    MemcachedConnectPool *memcPool = (MemcachedConnectPool*)arg;
    MemcachedConnectPool::Proxy* proxy = memcPool->getProxy();
    
    memcached_return rc;
    char *key = "mykey";
    char *value = "myvalue";
    size_t value_length;
    uint32_t flags;
    
    while (1)
    {
        // 获取连接
        memcached_st *memc = proxy->take();
        
        if (!memc) {
            fprintf(stderr, "can't take memcached handle\n");
            return NULL;
        }
        
        rc = memcached_set(memc, key, strlen(key), value, strlen(value), (time_t)0, (uint32_t)0);
        
        if (rc == MEMCACHED_SUCCESS) {
            //fprintf(stderr, "Key stored successfully\n");
        } else {
            fprintf(stderr, "Couldn't store key: %s\n", memcached_strerror(memc, rc));
            proxy->put(memc, true);
            return NULL;
        }
        
        char *retrieved_value = memcached_get(memc, key, strlen(key), &value_length, &flags, &rc);
        
        if (rc == MEMCACHED_SUCCESS) {
            //printf("The key '%s' returned value '%s'.\n", key, retrieved_value);
            free(retrieved_value);
        }
        else {
            fprintf(stderr, "Couldn't retrieve key: %s\n", memcached_strerror(memc, rc));
            proxy->put(memc, true);
            return NULL;
        }
        
        g_cnt++;
        
        // 归还连接
        proxy->put(memc, false);
    }
    
    return NULL;
}

void *timerTask(void * arg) {
    co_enable_hook_sys();
    
    while (1) {
        sleep(1);
        
        printf("======================\n");
    }
    
    return NULL;
}

void clientThread(MemcachedConnectPool *memcPool) {
    // 开多个个协程
    for (int i=0; i<2; i++) {
        RoutineEnvironment::startCoroutine(memc_routine, memcPool);
    }
    
    printf("running...\n");
    
    corpc::RoutineEnvironment::startCoroutine(timerTask, NULL);
    //corpc::RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop(100);
}


int main(int argc, const char * argv[]) {
    co_start_hook();
    
    memcached_return rc;
    memcached_server_st *servers = NULL;
    servers = memcached_server_list_append(servers, "localhost", 11211, &rc);
    
    MemcachedConnectPool *memcPool = MemcachedConnectPool::create(servers, 4, 2);
    
    // 开两个线程进行多线程访问
    std::thread t1 = std::thread(clientThread, memcPool);
    std::thread t2 = std::thread(clientThread, memcPool);
    
    corpc::RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop();
    
    return 0;
}

