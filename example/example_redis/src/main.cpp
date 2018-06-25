//
//  main.cpp
//  example_redis
//
//  Created by Xianke Liu on 2018/5/16.
//  Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_redis.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static void *redis_routine( void *arg )
{
    co_enable_hook_sys();
    
    RedisConnectPool *redisPool = (RedisConnectPool*)arg;
    RedisConnectPool::Proxy* proxy = redisPool->getProxy();
    
    redisReply *reply;
    while (1)
    {
        // 获取连接
        redisContext *redis = proxy->take();
        
        if (!redis) {
            fprintf(stderr, "can't take redis handle\n");
            return NULL;
        }
        
        // PING server
        reply = (redisReply *)redisCommand(redis,"PING");
        //printf("PING: %s\n", reply->str);
        freeReplyObject(reply);
        
        // Set a key
        reply = (redisReply *)redisCommand(redis,"SET %s %s", "foo", "hello world");
        //printf("SET: %s\n", reply->str);
        freeReplyObject(reply);
        
        // Set a key using binary safe API
        reply = (redisReply *)redisCommand(redis,"SET %b %b", "bar", (size_t) 3, "hello", (size_t) 5);
        //printf("SET (binary API): %s\n", reply->str);
        freeReplyObject(reply);
        
        // Try a GET and two INCR
        reply = (redisReply *)redisCommand(redis,"GET foo");
        //printf("GET foo: %s\n", reply->str);
        freeReplyObject(reply);
        
        reply = (redisReply *)redisCommand(redis,"INCR counter");
        //printf("INCR counter: %lld\n", reply->integer);
        freeReplyObject(reply);
        // again ...
        reply = (redisReply *)redisCommand(redis,"INCR counter");
        //printf("INCR counter: %lld\n", reply->integer);
        freeReplyObject(reply);
        
        // Create a list of numbers, from 0 to 9
        reply = (redisReply *)redisCommand(redis,"DEL mylist");
        freeReplyObject(reply);
        for (int j = 0; j < 10; j++) {
            char buf[64];
            
            snprintf(buf,64,"%u",j);
            reply = (redisReply *)redisCommand(redis,"LPUSH mylist element-%s", buf);
            freeReplyObject(reply);
        }
        
        // Let's check what we have inside the list
        reply = (redisReply *)redisCommand(redis,"LRANGE mylist 0 -1");
        if (reply->type == REDIS_REPLY_ARRAY) {
            for (int j = 0; j < reply->elements; j++) {
                //printf("%u) %s\n", j, reply->element[j]->str);
            }
        }
        freeReplyObject(reply);
        
        g_cnt++;
        
        // 归还连接
        proxy->put(redis, false);
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

void clientThread(RedisConnectPool *redisPool) {
    // 开多个个协程
    for (int i=0; i<4; i++) {
        RoutineEnvironment::startCoroutine(redis_routine, redisPool);
    }
    
    printf("running...\n");
    
    corpc::RoutineEnvironment::startCoroutine(timerTask, NULL);
    //corpc::RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop(100);
}


int main(int argc, const char * argv[]) {
    co_start_hook();
    
    RedisConnectPool *redisPool = RedisConnectPool::create("127.0.0.1", 6379, 8, 4);
    
    // 开两个线程进行多线程访问
    std::thread t1 = std::thread(clientThread, redisPool);
    std::thread t2 = std::thread(clientThread, redisPool);
    
    corpc::RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop();
    
    return 0;
}
