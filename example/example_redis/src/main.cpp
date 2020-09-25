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
        
        LOG("time %ld seconds, cnt: %d, average: %d, total: %d\n", difTime, int(g_cnt), average, total);
        
        g_cnt = 0;
    }
    
    return NULL;
}

static void *redis_routine( void *arg )
{
    co_enable_hook_sys();
    
    RedisConnectPool *redisPool = (RedisConnectPool*)arg;
    RedisConnectPool::Proxy& proxy = redisPool->proxy;
    
    redisReply *reply;
    while (1)
    {
        // 获取连接
        redisContext *redis = proxy.take();
        
        if (!redis) {
            ERROR_LOG("can't take redis handle\n");
            return NULL;
        }
        
        // PING server
        reply = (redisReply *)redisCommand(redis,"PING");
        //LOG("PING: %s\n", reply->str);
        freeReplyObject(reply);
        
        // Set a key
        reply = (redisReply *)redisCommand(redis,"SET %s %s", "foo", "hello world");
        //LOG("SET: %s\n", reply->str);
        freeReplyObject(reply);
        
        // Set a key using binary safe API
        reply = (redisReply *)redisCommand(redis,"SET %b %b", "bar", (size_t) 3, "hello", (size_t) 5);
        //LOG("SET (binary API): %s\n", reply->str);
        freeReplyObject(reply);
        
        // Try a GET and two INCR
        reply = (redisReply *)redisCommand(redis,"GET foo");
        //LOG("GET foo: %s\n", reply->str);
        freeReplyObject(reply);
        
        reply = (redisReply *)redisCommand(redis,"INCR counter");
        //LOG("INCR counter: %lld\n", reply->integer);
        freeReplyObject(reply);
        // again ...
        reply = (redisReply *)redisCommand(redis,"INCR counter");
        //LOG("INCR counter: %lld\n", reply->integer);
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
        proxy.put(redis, false);
    }
    
    return NULL;
}

struct TestStruct {
    uint32_t id;
    bool bantalk;
    bool banlogin;
};

static void *redis_routine1( void *arg )
{
    co_enable_hook_sys();
    LOG("1\n");
    RedisConnectPool *redisPool = (RedisConnectPool*)arg;
    RedisConnectPool::Proxy& proxy = redisPool->proxy;
    
    redisReply *reply;
    
    // 获取连接
    redisContext *redis = proxy.take();
    
    LOG("2\n");
    if (!redis) {
        ERROR_LOG("can't take redis handle\n");
        return NULL;
    }
    
    sleep(10);
    
    LOG("3\n");
    reply = (redisReply *)redisCommand(redis,"WATCH hahaha");
    LOG("WATCH: %d\n", reply->type);
    freeReplyObject(reply);
    
    reply = (redisReply *)redisCommand(redis,"SET hahaha abc");
    LOG("SET: %d\n", reply->type);
    freeReplyObject(reply);
    
    reply = (redisReply *)redisCommand(redis,"MULTI");
    LOG("MULTI: %d\n", reply->type);
    freeReplyObject(reply);
    
    reply = (redisReply *)redisCommand(redis,"DEL hahaha");
    LOG("DEL: %d\n", reply->type);
    freeReplyObject(reply);
    
    reply = (redisReply *)redisCommand(redis,"EXEC");
    LOG("EXEC: %d\n", reply->type);
    freeReplyObject(reply);
    /*
    TestStruct ts;
    ts.id = 1000;
    ts.bantalk = true;
    ts.banlogin = false;
    reply = (redisReply *)redisCommand(redis,"SET hahaha %b", &ts, sizeof(ts));
    //LOG("SET: %s\n", reply->str);
    freeReplyObject(reply);
    
    TestStruct ts_get;
    reply = (redisReply *)redisCommand(redis,"GET hahaha");
    assert(reply->len == sizeof(ts));
    memcpy(&ts_get, reply->str, sizeof(ts));
    LOG("GET hahaha: %d\n", ts_get.id);
    freeReplyObject(reply);
     */
    
    // 归还连接
    proxy.put(redis, false);
    
    
    return NULL;
}

void *timerTask(void * arg) {
    co_enable_hook_sys();
    
    while (1) {
        sleep(1);
        
        LOG("======================\n");
    }
    
    return NULL;
}

void clientThread(RedisConnectPool *redisPool) {
    // 开多个个协程
    for (int i=0; i<4; i++) {
        RoutineEnvironment::startCoroutine(redis_routine, redisPool);
    }
    
    LOG("running...\n");
    
    corpc::RoutineEnvironment::startCoroutine(timerTask, NULL);
    //corpc::RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop();
}


int main(int argc, const char * argv[]) {
    co_start_hook();
    
    RedisConnectPool *redisPool = RedisConnectPool::create("192.168.92.3", 6379, 8);
    
    /*
    // 开两个线程进行多线程访问
    std::thread t1 = std::thread(clientThread, redisPool);
    std::thread t2 = std::thread(clientThread, redisPool);
    
    corpc::RoutineEnvironment::startCoroutine(log_routine, NULL);
    */
    
    RoutineEnvironment::startCoroutine(redis_routine1, redisPool);
    RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop();
    
    return 0;
}
