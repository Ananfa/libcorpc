//
//  main.cpp
//  example_redis
//
//  Created by Xianke Liu on 2020/9/24.
//  Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_pubsub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>

using namespace corpc;

static void subCallback(const std::string& topic, const std::string& msg) {
    LOG("get topic: %s, message: %s\n", topic.c_str(), msg.c_str());
}

static void *log_routine( void *arg )
{
    co_enable_hook_sys();
    
    while (true) {
        sleep(1);
        LOG("=======\n");
    }
    
    return NULL;
}

void subThread() {
    LOG("subThread running...\n");
    
    PubsubService::Subscribe("test_topic2", false, subCallback);
    PubsubService::Subscribe("test_topic3", false, subCallback);
    PubsubService::Subscribe("test_topic4", true, subCallback);
    
    RoutineEnvironment::runEventLoop();
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    RedisConnectPool *redisPool = RedisConnectPool::create("192.168.92.221", 6379, 0, 8);
    std::list<std::string> topics;
    topics.push_back("test_topic1");
    topics.push_back("test_topic2");
    topics.push_back("test_topic3");
    topics.push_back("test_topic4");
    PubsubService::StartPubsubService(redisPool, topics);
    PubsubService::Subscribe("test_topic1", false, subCallback);
    PubsubService::Subscribe("test_topic2", true, subCallback);
    PubsubService::Subscribe("test_topic3", true, subCallback);
    RoutineEnvironment::startCoroutine(log_routine, NULL);

    //std::thread t1 = std::thread(subThread);
    
    RoutineEnvironment::runEventLoop();
    
    return 0;
}
