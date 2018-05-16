//
//  main.cpp
//  example_mongodb
//
//  Created by Xianke Liu on 2018/5/16.
//  Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_mongodb.h"
#include "corpc_utils.h"
#include <bson.h>
#include <mongoc.h>
#include <stdio.h>

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

static void *mongodb_routine( void *arg )
{
    co_enable_hook_sys();
    
    MongodbConnectPool *mongodbPool = (MongodbConnectPool*)arg;
    MongodbConnectPool::Proxy* proxy = mongodbPool->getProxy();
    
    while (1)
    {
        // 获取连接
        mongoc_client_t *mongoc = proxy->take();
        
        if (!mongoc) {
            fprintf(stderr, "can't take mongoc handle\n");
            return NULL;
        }
        
        mongoc_collection_t *collection = mongoc_client_get_collection(mongoc, "db_name", "coll_name");
        
        bson_t *query = bson_new ();
        mongoc_cursor_t *cursor = mongoc_collection_find_with_opts (collection, query, NULL, NULL);
        const bson_t *doc;
        char *str;
        while (mongoc_cursor_next (cursor, &doc)) {
            str = bson_as_canonical_extended_json (doc, NULL);
            //printf ("%s\n", str);
            bson_free (str);
        }
        
        bson_destroy (query);
        mongoc_cursor_destroy (cursor);
        
        mongoc_collection_destroy (collection);
        
        g_cnt++;
        
        // 归还连接
        proxy->put(mongoc, false);
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

void clientThread(MongodbConnectPool *mongodbPool) {
    // 开多个个协程
    for (int i=0; i<4; i++) {
        RoutineEnvironment::startCoroutine(mongodb_routine, mongodbPool);
    }
    
    printf("running...\n");
    
    corpc::RoutineEnvironment::startCoroutine(timerTask, NULL);
    //corpc::RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop(100);
}


int main(int argc, const char * argv[]) {
    co_start_hook();
    
    std::string uri = "mongodb://localhost:27017/?appname=find-example";
    MongodbConnectPool *mongodbPool = MongodbConnectPool::create(uri, 8, 4);
    
    // 开两个线程进行多线程访问
    std::thread t1 = std::thread(clientThread, mongodbPool);
    std::thread t2 = std::thread(clientThread, mongodbPool);
    
    corpc::RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop();
    
    return 0;
}


