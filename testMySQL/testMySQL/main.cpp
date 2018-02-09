//
//  main.m
//  testMySQL
//  此程序测试mysqlclient库与libco库的兼容性
//  启动两协程，一个进行数据库访问，另一个进行定时任务
//
//  Created by Xianke Liu on 2018/1/17.
//  Copyright © 2018年 Dena. All rights reserved.
//

#include "co_routine.h"

#include <stdio.h>
#include <mysql.h>

/*
#include <dlfcn.h>

typedef unsigned long (*mysql_get_client_version_fn)(void);
typedef const char* (*mysql_get_client_info_fn)(void);
typedef MYSQL* (*mysql_init_fn)(MYSQL*);
typedef const char* (*mysql_error_fn)(MYSQL*);
typedef MYSQL* (*mysql_real_connect_fn)(MYSQL*, const char*, const char*, const char*, const char*, unsigned int, const char*, unsigned long);
typedef void (*mysql_close_fn)(MYSQL*);
typedef int (*mysql_query_fn)(MYSQL*, const char*);
typedef unsigned int (*mysql_num_fields_fn)(MYSQL_RES*);
typedef MYSQL_FIELD* (*mysql_fetch_fields_fn)(MYSQL_RES*);
typedef MYSQL_ROW (*mysql_fetch_row_fn)(MYSQL_RES*);
typedef unsigned long * (*mysql_fetch_lengths_fn)(MYSQL_RES *result);
typedef MYSQL_RES* (*mysql_store_result_fn)(MYSQL*);
typedef void (*mysql_free_result_fn)(MYSQL_RES*);


void* mysql_handle = NULL;

static mysql_get_client_version_fn __mysql_get_client_version = NULL;
static mysql_get_client_info_fn __mysql_get_client_info = NULL;
static mysql_init_fn __mysql_init = NULL;
static mysql_error_fn __mysql_error = NULL;
static mysql_real_connect_fn __mysql_real_connect = NULL;
static mysql_close_fn __mysql_close = NULL;
static mysql_query_fn __mysql_query = NULL;
static mysql_num_fields_fn __mysql_num_fields = NULL;
static mysql_fetch_fields_fn __mysql_fetch_fields = NULL;
static mysql_fetch_row_fn __mysql_fetch_row = NULL;
static mysql_fetch_lengths_fn __mysql_fetch_lengths = NULL;
static mysql_store_result_fn __mysql_store_result = NULL;
static mysql_free_result_fn __mysql_free_result = NULL;

static void __mysql_lib_load(void)
{
    mysql_handle = dlopen("/usr/local/mysql/lib/libmysqlclient.dylib", RTLD_LAZY);
    
    __mysql_get_client_version = (mysql_get_client_version_fn) dlsym(mysql_handle, "mysql_get_client_version");
    __mysql_get_client_info = (mysql_get_client_info_fn) dlsym(mysql_handle, "mysql_get_client_info");
    __mysql_init = (mysql_init_fn) dlsym(mysql_handle, "mysql_init");
    __mysql_error = (mysql_error_fn) dlsym(mysql_handle, "mysql_init");
    __mysql_real_connect = (mysql_real_connect_fn) dlsym(mysql_handle, "mysql_real_connect");
    __mysql_close = (mysql_close_fn) dlsym(mysql_handle, "mysql_close");
    __mysql_query = (mysql_query_fn) dlsym(mysql_handle, "mysql_query");
    __mysql_num_fields = (mysql_num_fields_fn) dlsym(mysql_handle, "mysql_num_fields");
    __mysql_fetch_fields = (mysql_fetch_fields_fn) dlsym(mysql_handle, "mysql_fetch_fields");
    __mysql_fetch_row = (mysql_fetch_row_fn) dlsym(mysql_handle, "mysql_fetch_row");
    __mysql_fetch_lengths = (mysql_fetch_lengths_fn) dlsym(mysql_handle, "mysql_fetch_lengths");
    __mysql_store_result = (mysql_store_result_fn) dlsym(mysql_handle, "mysql_store_result");
    __mysql_free_result = (mysql_free_result_fn) dlsym(mysql_handle, "mysql_free_result");
}

void finish_with_error(MYSQL *con)
{
    fprintf(stderr, "%s\n", __mysql_error(con));
    __mysql_close(con);
    exit(1);
}

void *mysqlTask(void *arg) {
    co_enable_hook_sys();
    
    __mysql_lib_load();
    
    MYSQL *con = __mysql_init(NULL);
    
    if (__mysql_real_connect(con,"127.0.0.1","root","123456","ss_s000",0,0,0) == NULL)
    {
        finish_with_error(con);
    }
    
    printf("连接成功, 服务器版本: %s, 客户端版本: %s.\n", MYSQL_SERVER_VERSION, __mysql_get_client_info());
    
    while (1)
    {
        if (__mysql_query(con, "SELECT bindata FROM scenedb_account WHERE field1='72057594037927957'"))
        {
            finish_with_error(con);
        }
        
        MYSQL_RES *result = __mysql_store_result(con);
        
        if (result == NULL)
        {
            finish_with_error(con);
        }
        
        MYSQL_ROW row;
        
        while ((row = __mysql_fetch_row(result)))
        {
            unsigned long *lengths = __mysql_fetch_lengths(result);
            
            if (lengths == NULL) {
                finish_with_error(con);
            }
            
            printf("data length %lu \n", lengths[0]);
        }
        
        __mysql_free_result(result);
    }
    
    __mysql_close(con);
    
    return NULL;
}
*/
void finish_with_error(MYSQL *con)
{
    fprintf(stderr, "%s\n", mysql_error(con));
    mysql_close(con);
    exit(1);
}

void *mysqlTask(void *arg) {
    co_enable_hook_sys();
    
    MYSQL *con = mysql_init(NULL);
    
    if (mysql_real_connect(con,"127.0.0.1","root","123456","ss_s000",0,0,0) == NULL)
    {
        finish_with_error(con);
    }
    
    printf("连接成功, 服务器版本: %s, 客户端版本: %s.\n", MYSQL_SERVER_VERSION, mysql_get_client_info());
    
    while (1)
    {
        if (mysql_query(con, "SELECT bindata FROM scenedb_account WHERE field1='72057594037927957'"))
        {
            finish_with_error(con);
        }
        
        MYSQL_RES *result = mysql_store_result(con);
        
        if (result == NULL)
        {
            finish_with_error(con);
        }
        
        MYSQL_ROW row;
        
        while ((row = mysql_fetch_row(result)))
        {
            unsigned long *lengths = mysql_fetch_lengths(result);
            
            if (lengths == NULL) {
                finish_with_error(con);
            }
            
            printf("data length %lu \n", lengths[0]);
        }
        
        mysql_free_result(result);
    }
    
    mysql_close(con);
    
    return NULL;
}

void *timerTask(void * arg) {
    co_enable_hook_sys();
    
    struct pollfd pf = { 0 };
    pf.fd = -1;
    while (1) {
        poll( &pf,1,1000);
        
        printf("======================\n");
    }
    
    return NULL;
}

int main(int argc, const char * argv[])
{
    start_hook();
    
    stCoRoutine_t *timerCo = 0;
    co_create( &timerCo,NULL,timerTask, NULL);
    co_resume( timerCo );
    
    stCoRoutine_t *mysqlCo = 0;
    co_create( &mysqlCo,NULL,mysqlTask, NULL);
    co_resume( mysqlCo );
    
    //std::thread t = std::thread(pipeSendTask, &myPipe);
    //stCoRoutine_t *sendCo = 0;
    //co_create( &sendCo,NULL,pipeSendTask, &myPipe);
    //co_resume( sendCo );
    
    co_eventloop( co_get_epoll_ct(),0,0 );
    
    return 0;
}
