//
//  co_rpc_routine_env.h
//  rpccli
//
//  Created by Xianke Liu on 2017/11/21.
//  Copyright © 2017年 Dena. All rights reserved.
//

#ifndef corpc_routine_env_h
#define corpc_routine_env_h

#include "co_routine.h"
#include "corpc_define.h"

#include <list>

namespace CoRpc {
    struct WaitingRoutine {
        pfn_co_routine_t pfn;
        void *arg;
    };
    
    // 协程环境是否应该与线程绑定，约定每个线程只有一个协程环境
    // 每个线程可初始化自己的协程环境，约定只能初始化一次，一般在线程开始时初始化
    class RoutineEnvironment {
        static const unsigned int MAX_COROUTINE_NUM = 1000;
        static const unsigned int SHARE_STACK_COUNT = 50;
        static const unsigned int SHARE_STACK_SIZE = 128 * 1024;
        
    public:
        static void destroy(); // 清理当前线程协程环境
        static stCoRoutine_t *startCoroutine(pfn_co_routine_t pfn,void *arg);
        static void runEventLoop( int max_wait_ms = 1000 ); // 事件循环
        
        static RoutineEnvironment *getEnv();    // 获取线程相关的协程环境
        stCoRoutineAttr_t *getAttr() { return _attr; }
        
    private:
        RoutineEnvironment();
        ~RoutineEnvironment();
        
        static RoutineEnvironment *initialize(); // 为当前线程创建协程环境
        
        static void *routineEntry( void *arg ); // 协程入口
        
        static void *deamonRoutine( void *arg ); // 守护协程（定期清理结束的协程，并启动等待的任务）
        
        void addEndedCoroutine( stCoRoutine_t *co ); // 协程结束
        
    private:
        stCoRoutineAttr_t *_attr;
        int _coCount;   // 协程数量（已启动的任务数量）
        
        std::list<WaitingRoutine*> _waitingRoutines; // 未启动的任务
        
        PipeType _endPipe; // 用于通知deamonRoutine“有已结束协程”
        std::list<stCoRoutine_t*> _endedCoroutines; // 已结束的协程（待清理的协程）
        
    };
    
}

#endif /* corpc_routine_env_h */
