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

#ifndef corpc_routine_env_h
#define corpc_routine_env_h

#include "co_routine.h"
#include "corpc_queue.h"
#include "corpc_rpc_common.h"
#include "corpc_timeout_list.h"

#include <list>

namespace corpc {
    struct RoutineContext {
        pfn_co_routine_t pfn;
        void *arg;
    };

    struct WaitResumeRPCRoutine {
        stCoRoutine_t* co;
        uint64_t expireTime;
        int err;
    };
    
    // 协程环境是否应该与线程绑定，约定每个线程只有一个协程环境
    // 每个线程可初始化自己的协程环境，约定只能初始化一次，一般在线程开始时初始化
    class RoutineEnvironment {
        static const unsigned int SHARE_STACK_COUNT = 50; //多个共享栈可以降低栈数据切换
        static const unsigned int SHARE_STACK_SIZE = 1024 * 1024;
        
#ifdef USE_NO_LOCK_QUEUE
        typedef Co_MPSC_NoLockQueue<WaitResumeRPCRoutine *> WaitResumeQueue;
#else
        typedef CoSyncQueue<WaitResumeRPCRoutine *> WaitResumeQueue;
#endif
        
    public:
        class Guard {
        public:
            Guard() { RoutineEnvironment::keyRoutineNum_++; }
            ~Guard() { RoutineEnvironment::keyRoutineNum_--; }
            
        private:
            Guard(Guard const&) = delete;                  // copy ctor delete
            Guard(Guard &&) = delete;                      // move ctor delete
            Guard& operator=(Guard const&) = delete;       // assign op. delete
            Guard& operator=(Guard &&) = delete;           // move assign op. delete
            
            void* operator new (std::size_t size) = delete;
        };
        
    public:
        //void destroy(); // 清理当前线程协程环境
        static void init();
        static stCoRoutine_t *startCoroutine(pfn_co_routine_t pfn,void *arg);
        static stCoRoutine_t *startKeyCoroutine(pfn_co_routine_t pfn,void *arg);
        static void resumeCoroutine( pid_t pid, stCoRoutine_t *co, uint64_t expireTime = 0, int err = 0 ); // 用于跨线程唤醒RPC协程
        static void runEventLoop(); // 事件循环
        
        static RoutineEnvironment *getEnv();    // 获取线程相关的协程环境
        stCoRoutineAttr_t *getAttr() { return attr_; }
        
        static void quit();
        
        static void addTimeoutTask( std::shared_ptr<RpcClientTask>& rpcTask );
        static void pause(); // 当前协程暂停并下一运行时循环继续执行
        static void pauseIfRuntimeBusy(); // 当运行时忙时暂停并在下一运行时循环继续执行

    private:
        RoutineEnvironment();
        ~RoutineEnvironment();
        
        static RoutineEnvironment *initialize(); // 为当前线程创建协程环境
        
        static void *routineEntry( void *arg ); // 协程入口
        
        static void *keyRoutineEntry( void *arg ); // 关键协程入口
        
        static void *cleanRoutine( void *arg ); // 协程清理协程
        
        static void *resumeRoutine( void *arg ); // 协程唤醒协程

        static void *timeoutRoutine( void *arg ); // RPC超时处理协程
        
        static void *safeQuitRoutine( void *arg ); // 安全退出程序协程（等待_keyRoutineNum计数为0时退出程序）
        
        void addEndedCoroutine( stCoRoutine_t *co ); // 协程结束
        
    private:
        stCoRoutineAttr_t *attr_;
        
        PipeType _endPipe; // 用于通知cleanRoutine“有已结束协程”
        std::list<stCoRoutine_t*> endedCoroutines_; // 已结束的协程（待清理的协程）
        
        WaitResumeQueue waitResumeQueue_; // 用于跨线程唤醒协程

        TimeoutList<std::shared_ptr<RpcClientTask>> timeoutList_; // 管理本线程中的RPC请求协程超时
        
        static std::atomic<uint32_t> keyRoutineNum_;
        
#ifdef MONITOR_ROUTINE
        // 监控状态
        static void *monitorRoutine( void *arg ); // 监控协程
        
        int routineNum_; // 当前协程总数量（包括活着的和等待消耗的协程）
        int livingRoutineNum_; // 活着的协程数量
#endif
    };
    
}

#endif /* corpc_routine_env_h */
