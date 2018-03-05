//
//  co_rpc_routine_env.cpp
//  rpccli
//
//  Created by Xianke Liu on 2017/11/21.
//  Copyright © 2017年 Dena. All rights reserved.
//

#include "corpc_routine_env.h"

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <vector>

namespace CoRpc {
    static RoutineEnvironment* g_routineEnvPerThread[ 204800 ] = { 0 };
    
    RoutineEnvironment::RoutineEnvironment() {
        _coCount = 0;
        
        _attr = new stCoRoutineAttr_t;
        _attr->stack_size = SHARE_STACK_SIZE;
        _attr->share_stack = co_alloc_sharestack(SHARE_STACK_COUNT, SHARE_STACK_SIZE);
        
        pipe(_endPipe.pipefd);
        co_register_fd(_endPipe.pipefd[1]);
        co_set_nonblock(_endPipe.pipefd[1]);
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
        
        // 启动deamon协程
        stCoRoutine_t *co = NULL;
        co_create( &co, env->_attr, deamonRoutine, env);
        co_resume( co );
        
        return env;
    }
    
    void RoutineEnvironment::destroy() {
        // TODO: 清理当前线程协程环境
        // 当线程结束时进行回收工作
        // 需进行的工作包括：
        //   1.禁止新协程创建
        //   2.等待正在运行的协程结束（这点不好处理，因为协程正注册在IO事件中等待，当IO事件发生时会唤醒协程执行，若此时协程已被清理则程序跑飞，一般只能等待协程自然结束，而协程中的处理很可能不会结束）
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
        WaitingRoutine *routine = new WaitingRoutine;
        routine->pfn = pfn;
        routine->arg = arg;
        if (curenv->_coCount < MAX_COROUTINE_NUM) {
            curenv->_coCount++;
            
            co_create( &co, curenv->_attr, routineEntry, routine);
            co_resume( co );
        } else {
            curenv->_waitingRoutines.push_back(routine);
        }
        
        return co;
    }
    
    void RoutineEnvironment::runEventLoop( int max_wait_ms ) {
        co_eventloop( co_get_epoll_ct(),0,0,max_wait_ms );
    }
    
    void *RoutineEnvironment::routineEntry( void *arg ) {
        co_enable_hook_sys();
        
        WaitingRoutine *routine = (WaitingRoutine*)arg;
        pfn_co_routine_t pfn = routine->pfn;
        void *ar = routine->arg;
        
        delete routine;
        
        pfn(ar);
        
        getEnv()->addEndedCoroutine(co_self());
        
        return NULL;
    }
    
    void *RoutineEnvironment::deamonRoutine( void *arg ) {
        co_enable_hook_sys();
        
        RoutineEnvironment *curenv = (RoutineEnvironment *)arg;
        
        int pReadFd = curenv->_endPipe.pipefd[0];
        co_register_fd(pReadFd);
        co_set_timeout(pReadFd, -1, 1000);
        
        int ret;
        std::vector<char> buf(1024);
        while (true) {
            // 等待处理信号
            ret = read(pReadFd, &buf[0], 1024);
            assert(ret != 0);
            if (ret < 0) {
                if (errno == EAGAIN) {
                    continue;
                } else {
                    // 管道出错
                    printf("Error: RoutineEnvironment::deamonRoutine read from up pipe fd %d ret %d errno %d (%s)\n",
                           pReadFd, ret, errno, strerror(errno));
                    
                    // TODO: 如何处理？退出协程？
                    // sleep 10 milisecond
                    struct pollfd pf = { 0 };
                    pf.fd = -1;
                    poll( &pf,1,10);
                }
            }
            
            // 处理结束协程
            while (!curenv->_endedCoroutines.empty()) {
                stCoRoutine_t *co = curenv->_endedCoroutines.front();
                curenv->_endedCoroutines.pop_front();
                
                curenv->_coCount--;
                assert(curenv->_coCount >= 0);
                
                co_release(co);
            }
            
            // 启动待处理任务
            while (curenv->_coCount < MAX_COROUTINE_NUM && !curenv->_waitingRoutines.empty()) {
                WaitingRoutine *routine = curenv->_waitingRoutines.front();
                curenv->_waitingRoutines.pop_front();
                
                curenv->_coCount++;
                
                stCoRoutine_t *co = NULL;
                co_create( &co, curenv->_attr, routineEntry, routine);
                co_resume( co );
            }
        }
        
        return NULL;
    }
    
    void RoutineEnvironment::addEndedCoroutine( stCoRoutine_t *co ) {
        _endedCoroutines.push_back(co);
        
        char buf = 'L';
        write(_endPipe.pipefd[1], &buf, 1);
    }
}
