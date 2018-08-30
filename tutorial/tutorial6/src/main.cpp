//
//  main.cpp
//  tutorial6
//
//  Created by Xianke Liu on 2018/8/30.
//  Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_routine_env.h"
#include <random>

using namespace corpc;

void *keyRoutine(void *arg) {
    uint64_t i = (uint64_t)arg;
    LOG("key Routine %d begin\n",i);
    sleep(rand() % 10 + 1);
    LOG("key Routine %d end\n",i);
    return NULL;
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    for (uint64_t i = 1; i <= 10; i++) {
        RoutineEnvironment::startKeyCoroutine(keyRoutine, (void*)i);
    }
    
    RoutineEnvironment::quit();
    
    RoutineEnvironment::runEventLoop();
}
