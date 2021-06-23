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

#include "corpc_routine_env.h"

#include <vector>
#include <thread>

using namespace corpc;

int pipefd[2];

static void *thread1_routine( void *arg )
{
    co_enable_hook_sys();
    
    LOG("thread1_routine begin\n");
    //char buf = 'X';
    //write(pipefd[1], &buf, 1);
    close(pipefd[0]);
    close(pipefd[1]);
    
    LOG("thread1_routine end\n");
    
    return NULL;
}

void thread1() {
    LOG("thread1 running...\n");
    
    RoutineEnvironment::startCoroutine(thread1_routine, NULL);
    
    RoutineEnvironment::runEventLoop();
}

int num = 2;

static void *thread2_routine( void *arg )
{
    co_enable_hook_sys();
    
    LOG("thread2_routine begin, co:%d\n", co_self());
    
    sleep(5);
    std::vector<char> buf(1024);

    while (true) {
        int ret = (int)read(pipefd[0], &buf[0], 1024);
        LOG("thread2_routine co:%d recv data size:%d\n", co_self(), ret);
        if (ret < 0) {
            if (errno == EAGAIN) {
                LOG("thread2_routine errno == EAGAIN, co:%d\n", co_self());
                continue;
            }

            ERROR_LOG("thread2_routine errno == %d, co:%d\n", errno, co_self());
            break;
        }

        if (num > 0) {
            num--;
            char buf1 = 'X';
            write(pipefd[1], &buf1, 1);
        }
        break;
    }
    
    LOG("thread2_routine end, co:%d\n", co_self());
    
    return NULL;
}

void thread2() {
    LOG("thread2 %d running...\n", GetPid());
    
    RoutineEnvironment::startCoroutine(thread2_routine, NULL);
    
    RoutineEnvironment::runEventLoop();
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    pipe(pipefd);
    co_register_fd(pipefd[1]);
    co_register_fd(pipefd[0]);
    co_set_nonblock(pipefd[1]);
    co_set_timeout(pipefd[0], -1, 1000);

    int clientNum = 1;
    std::vector<std::thread> threads;
    for (int i = 0; i < clientNum; i++) {
        threads.push_back(std::thread(thread2));
    }

    // 新开一线程，并在其中开一组rpc_routine协程
    std::thread t1 = std::thread(thread1);
    
    // 注意：线程开多了性能不一定会增加，也可能降低，因此在具体项目中需要根据CPU核心数来调整线程数量
    
    //RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop();
    
    return 0;
}
