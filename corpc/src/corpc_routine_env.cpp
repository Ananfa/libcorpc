/*
 * Created by Xianke Liu on 2017/11/21.
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
#include "corpc_utils.h"

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <vector>
#include <sys/time.h>

namespace corpc {
    static RoutineEnvironment* g_routineEnvPerThread[ 204800 ] = { 0 };
    
    RoutineEnvironment::RoutineEnvironment() {
        _attr = new stCoRoutineAttr_t;
        _attr->stack_size = SHARE_STACK_SIZE;
        _attr->share_stack = co_alloc_sharestack(SHARE_STACK_COUNT, SHARE_STACK_SIZE);
        
        pipe(_endPipe.pipefd);
        co_register_fd(_endPipe.pipefd[1]);
        co_set_nonblock(_endPipe.pipefd[1]);
        
#ifdef MONITOR_ROUTINE
        _routineNum = 0;
        _livingRoutineNum = 0;
#endif
    }

    RoutineEnvironment::~RoutineEnvironment() {
        if (_attr) {
            delete _attr;
        }
    }
    
    RoutineEnvironment *RoutineEnvironment::getEnv() {
        RoutineEnvironment *env = g_routineEnvPerThread[GetPid()];
        if (!env) {
            env = initialize();
        }
        
        return env;
    }

    RoutineEnvironment *RoutineEnvironment::initialize() {
        pid_t pid = GetPid();
        assert(!g_routineEnvPerThread[pid]);
        
        printf("initialize env for pid: %d\n", pid);
        
        RoutineEnvironment *env = new RoutineEnvironment();
        g_routineEnvPerThread[pid] = env;
        
        stCoRoutine_t *co = NULL;
        
#ifdef MONITOR_ROUTINE
        // 启动监控协程
        co_create( &co, env->_attr, monitorRoutine, env);
        co_resume( co );
#endif
        
        // 启动clean协程
        co_create( &co, env->_attr, cleanRoutine, env);
        co_resume( co );
        
        // 启动resume协程
        co_create( &co, env->_attr, resumeRoutine, env);
        co_resume( co );
        
        return env;
    }
    
    void RoutineEnvironment::destroy() {
        // TODO: 清理当前线程协程环境
        // 当线程结束时进行回收工作
        // 需进行的工作包括：
        //   1.禁止新协程创建
        //   2.等待正在运行的协程结束（这点不好处理，原因：1.协程正注册在IO事件中等待，当IO事件发生时会唤醒协程执行，若此时协程已被清理则程序跑飞，2.程序员在协程中在堆中创建的对象无法释放，导致资源泄漏。一般只能等待协程自然结束，而协程中的处理很可能不会结束）
        //   3.守护协程结束
        //   4.从g_routineEnvPerThread中清理线程协程环境
        //   5.delete this;
        //
        // 由于第2点不好处理，而且一般情况开的线程不需要结束，因此先不实现
    }
    
    stCoRoutine_t *RoutineEnvironment::startCoroutine(pfn_co_routine_t pfn,void *arg) {
        RoutineEnvironment *curenv = getEnv();
        assert(curenv);
        
        // TODO: 若禁止新协程创建则返回false
        
        stCoRoutine_t *co = NULL;
        RoutineContext *context = new RoutineContext;
        context->pfn = pfn;
        context->arg = arg;
        
        co_create( &co, curenv->_attr, routineEntry, context);
        co_resume( co );
        
        return co;
    }
    
    void RoutineEnvironment::resumeCoroutine( pid_t pid, stCoRoutine_t *co ) {
        RoutineEnvironment *env = g_routineEnvPerThread[pid];
        assert(env);
        
        env->_waitResumeQueue.push(co);
    }
    
    void RoutineEnvironment::runEventLoop() {
        co_eventloop( co_get_epoll_ct(),0,0 );
    }
    
    void *RoutineEnvironment::routineEntry( void *arg ) {
        co_enable_hook_sys();
        
#ifdef MONITOR_ROUTINE
        RoutineEnvironment *curenv = getEnv();
        curenv->_routineNum++;
        curenv->_livingRoutineNum++;
#endif
        
        RoutineContext *context = (RoutineContext*)arg;
        pfn_co_routine_t pfn = context->pfn;
        void *ar = context->arg;
        
        delete context;
        
        pfn(ar);
        
#ifdef MONITOR_ROUTINE
        curenv->_livingRoutineNum--;
#endif
        
        getEnv()->addEndedCoroutine(co_self());
        
        return NULL;
    }
    
    void *RoutineEnvironment::cleanRoutine( void *arg ) {
        co_enable_hook_sys();
        
        RoutineEnvironment *curenv = (RoutineEnvironment *)arg;
        
        int pReadFd = curenv->_endPipe.pipefd[0];
        co_register_fd(pReadFd);
        co_set_timeout(pReadFd, -1, 1000);
        
        int ret;
        std::vector<char> buf(1024);
        while (true) {
            // 等待处理信号
            ret = (int)read(pReadFd, &buf[0], 1024);
            assert(ret != 0);
            if (ret < 0) {
                if (errno == EAGAIN) {
                    continue;
                } else {
                    // 管道出错
                    printf("Error: RoutineEnvironment::cleanRoutine read from up pipe fd %d ret %d errno %d (%s)\n",
                           pReadFd, ret, errno, strerror(errno));
                    
                    // TODO: 如何处理？退出协程？
                    // sleep 10 milisecond
                    msleep(10);
                }
            }
            
            // 处理结束协程
            while (!curenv->_endedCoroutines.empty()) {
                stCoRoutine_t *co = curenv->_endedCoroutines.front();
                curenv->_endedCoroutines.pop_front();
                
                co_release(co);
                
#ifdef MONITOR_ROUTINE
                curenv->_routineNum--;
#endif
            }
        }
        
        return NULL;
    }
    
    void *RoutineEnvironment::resumeRoutine( void *arg ) {
        co_enable_hook_sys();
        
        RoutineEnvironment *curenv = (RoutineEnvironment *)arg;
        
        int readFd = curenv->_waitResumeQueue.getReadFd();
        co_register_fd(readFd);
        co_set_timeout(readFd, -1, 1000);
        
        int ret;
        std::vector<char> buf(1024);
        while (true) {
            // 等待处理信号
            ret = (int)read(readFd, &buf[0], 1024);
            assert(ret != 0);
            if (ret < 0) {
                if (errno == EAGAIN) {
                    continue;
                } else {
                    // 管道出错
                    printf("Error: RoutineEnvironment::resumeRoutine read from up pipe fd %d ret %d errno %d (%s)\n",
                           readFd, ret, errno, strerror(errno));
                    
                    // TODO: 如何处理？退出协程？
                    // sleep 10 milisecond
                    msleep(10);
                }
            }
            
            struct timeval t1,t2;
            gettimeofday(&t1, NULL);
            
            stCoRoutine_t *co = curenv->_waitResumeQueue.pop();
            while (co) {
                co_resume(co);
                
                // 防止其他协程（如：RoutineEnvironment::cleanRoutine）长时间不被调度，这里在处理一段时间后让出一下
                gettimeofday(&t2, NULL);
                if ((t2.tv_sec - t1.tv_sec) * 1000000 + t2.tv_usec - t1.tv_usec > 100000) {
                    msleep(1);
                    
                    gettimeofday(&t1, NULL);
                }
                
                co = curenv->_waitResumeQueue.pop();
            }
        }
        
        return NULL;
    }
    
#ifdef MONITOR_ROUTINE
    void *RoutineEnvironment::monitorRoutine( void *arg ) {
        co_enable_hook_sys();
        
        RoutineEnvironment *curenv = (RoutineEnvironment *)arg;
        
        while( true ) {
            sleep(1);
            
            printf("monitorRoutine -- env: %ld, living: %d, dead: %d\n", curenv, curenv->_livingRoutineNum, curenv->_routineNum - curenv->_livingRoutineNum);
        }
        
        return NULL;
    }
#endif
    
    void RoutineEnvironment::addEndedCoroutine( stCoRoutine_t *co ) {
        _endedCoroutines.push_back(co);
        
        char buf = 'L';
        write(_endPipe.pipefd[1], &buf, 1);
    }
}