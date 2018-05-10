//
//  main.cpp
//  example_mysql
//
//  Created by Xianke Liu on 2018/5/8.
//  Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_mysql.h"
#include "corpc_utils.h"

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

static void *mysql_routine( void *arg )
{
    co_enable_hook_sys();
    
    MysqlConnectPool *mysqlPool = (MysqlConnectPool*)arg;
    MysqlConnectPool::Proxy* proxy = mysqlPool->getProxy();
    
    while (1)
    {
        // 获取连接
        MYSQL* con = proxy->take();
        
        if (!con) {
            fprintf(stderr, "cant connect to database");
            return NULL;
        }
        
        if (mysql_query(con, "SELECT bindata FROM scenedb_account WHERE field1='72057594037927957'"))
        {
            fprintf(stderr, "%s\n", mysql_error(con));
            // 出错返回处理
            proxy->put(con, true);
            return NULL;
        }
        
        MYSQL_RES *result = mysql_store_result(con);
        
        if (result == NULL)
        {
            fprintf(stderr, "%s\n", mysql_error(con));
            // 出错返回处理
            proxy->put(con, true);
            return NULL;
        }
        
        MYSQL_ROW row;
        
        while ((row = mysql_fetch_row(result)))
        {
            unsigned long *lengths = mysql_fetch_lengths(result);
            
            if (lengths == NULL) {
                fprintf(stderr, "%s\n", mysql_error(con));
                // 出错返回处理
                proxy->put(con, true);
                return NULL;
            }
            
            g_cnt++;
            //printf("data length %lu \n", lengths[0]);
        }
        
        mysql_free_result(result);
        
        // 归还连接
        proxy->put(con, false);
    }
    
    return NULL;
}

void clientThread(MysqlConnectPool *mysqlPool) {
    // 开多个个协程
    for (int i=0; i<4; i++) {
        RoutineEnvironment::startCoroutine(mysql_routine, mysqlPool);
    }
    
    printf("running...\n");
    
    //corpc::RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop(100);
}


int main(int argc, const char * argv[]) {
    co_start_hook();
    
    MysqlConnectPool *mysqlPool = MysqlConnectPool::create("127.0.0.1","root","123456","ss_s000",0,"",0, 4, 2);
    
    // 开两个线程进行多线程访问
    std::thread t1 = std::thread(clientThread, mysqlPool);
    std::thread t2 = std::thread(clientThread, mysqlPool);
    
    corpc::RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop();
    
    return 0;
}
