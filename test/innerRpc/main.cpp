//
//  main.cpp
//  testInnerRpc
//
//  Created by Xianke Liu on 2018/3/2.
//  Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_routine_env.h"
#include "corpc_controller.h"
#include "corpc_inner.h"

#include "foo.pb.h"
#include "bar.pb.h"
#include "baz.pb.h"

#include <thread>

using namespace CoRpc;

class FooServiceImpl : public FooService {
public:
    FooServiceImpl() {}
    virtual void Foo(::google::protobuf::RpcController* controller,
                     const ::FooRequest* request,
                     ::FooResponse* response,
                     ::google::protobuf::Closure* done) {
        std::string str = request->text();
        std::string tmp = str;
        for (int i = 1; i < request->times(); i++)
            str += (" " + tmp);
        response->set_text(str);
        response->set_result(true);
    }
};

class BarServiceImpl : public BarService {
public:
    BarServiceImpl() {}
    virtual void Bar(::google::protobuf::RpcController* controller,
                     const ::BarRequest* request,
                     ::BarResponse* response,
                     ::google::protobuf::Closure* done) {
        std::string str = request->text();
        std::string tmp = str;
        for (int i = 1; i < request->times(); i++)
            str += (" " + tmp);
        response->set_text(str);
        response->set_result(true);
    }
};

class BazServiceImpl : public BazService {
public:
    BazServiceImpl() {}
    virtual void Baz(::google::protobuf::RpcController* controller,
                     const ::BazRequest* request,
                     ::corpc::Void* response,
                     ::google::protobuf::Closure* done) {
        std::string str = request->text();
        
        //printf("BazServiceImpl::Baz: %s\n", str.c_str());
    }
};

static FooServiceImpl g_fooService;
static BarServiceImpl g_barService;
static BazServiceImpl g_bazService;

static int iFooSuccCnt = 0;
static int iFooFailCnt = 0;
static int iBarSuccCnt = 0;
static int iBarFailCnt = 0;
static int iBazSuccCnt = 0;
static int iBazFailCnt = 0;

struct Test_Stubs {
    FooService::Stub *foo_clt;
    BarService::Stub *bar_clt;
    BazService::Stub *baz_clt;
};

Test_Stubs g_stubs;

static void *log_routine( void *arg )
{
    co_enable_hook_sys();
    
    int totalSucc = 0;
    int totalFail = 0;
    int averageSucc = 0;
    
    time_t startAt = time(NULL);
    
    while (true) {
        sleep(1);
        
        totalSucc += iFooSuccCnt + iBarSuccCnt + iBazSuccCnt;
        totalFail += iFooFailCnt + iBarFailCnt + iBazFailCnt;
        
        time_t now = time(NULL);
        
        time_t difTime = now - startAt;
        if (difTime > 0) {
            averageSucc = totalSucc / difTime;
        } else {
            averageSucc = totalSucc;
        }
        
        printf("time %ld seconds, foo:Succ %d Fail %d, bar:Succ %d Fail %d, baz:Succ %d Fail %d, average:Succ %d\n", difTime, iFooSuccCnt, iFooFailCnt, iBarSuccCnt, iBarFailCnt, iBazSuccCnt, iBazFailCnt, averageSucc);
        
        iFooSuccCnt = 0;
        iFooFailCnt = 0;
        iBarSuccCnt = 0;
        iBarFailCnt = 0;
        iBazSuccCnt = 0;
        iBazFailCnt = 0;
    }
    
    return NULL;
}

static void *rpc_routine( void *arg )
{
    co_enable_hook_sys();
    
    Test_Stubs *testStubs = (Test_Stubs*)arg;
    
    // 注意：用于rpc调用参数的request,response和controller对象不能在栈中分配，必须在堆中分配，
    //      这是由于共享栈协程模式下，协程切换时栈会被当前协程栈覆盖，导致指向栈中地址的指针已经不是原来的对象
    while (true) {
        int type = rand() % 3;
        switch (type) {
            case 0: {
                FooRequest *request = new FooRequest();
                FooResponse *response = new FooResponse();
                Controller *controller = new Controller();
                
                request->set_text("Hello Foo");
                request->set_times(1);
                
                do {
                    controller->Reset();
                    testStubs->foo_clt->Foo(controller, request, response, NULL);
                    
                    if (controller->Failed()) {
                        //printf("Rpc Call Failed : %s\n", controller->ErrorText().c_str());
                        iFooFailCnt++;
                        
                        usleep(100000);
                    } else {
                        //printf("++++++ Rpc Response is %s\n", response->text().c_str());
                        iFooSuccCnt++;
                    }
                } while (controller->Failed());
                
                delete controller;
                delete response;
                delete request;
                break;
            }
            case 1: {
                BarRequest *request = new BarRequest();
                BarResponse *response = new BarResponse();
                Controller *controller = new Controller();
                
                request->set_text("Hello Bar");
                request->set_times(1);
                
                do {
                    controller->Reset();
                    testStubs->bar_clt->Bar(controller, request, response, NULL);
                    
                    if (controller->Failed()) {
                        //printf("Rpc Call Failed : %s\n", controller->ErrorText().c_str());
                        iBarFailCnt++;
                        
                        usleep(100000);
                    } else {
                        //printf("++++++ Rpc Response is %s\n", response->text().c_str());
                        iBarSuccCnt++;
                    }
                } while (controller->Failed());
                
                delete controller;
                delete response;
                delete request;
                
                break;
            }
            case 2: {
                BazRequest *request = new BazRequest();
                Controller *controller = new Controller();
                
                request->set_text("Hello Baz");
                
                do {
                    controller->Reset();
                    testStubs->baz_clt->Baz(controller, request, NULL, NULL);
                    
                    if (controller->Failed()) {
                        //printf("Rpc Call Failed : %s\n", controller->ErrorText().c_str());
                        iBazFailCnt++;
                        
                        usleep(100000);
                    } else {
                        //printf("++++++ Rpc Response is %s\n", response->text().c_str());
                        iBazSuccCnt++;
                    }
                } while (controller->Failed());
                
                delete controller;
                delete request;
                
                break;
            }
            default:
                break;
        }
    }
    
    return NULL;
}

static void *test_routine( void *arg )
{
    co_enable_hook_sys();
    
    printf("test_routine begin\n");
    
    for (int i=0; i<500; i++) {
        RoutineEnvironment::startCoroutine(rpc_routine, arg);
    }
    
    return NULL;
}

static void clientEntry( Inner::Server *server ) {
    Inner::Client *client = Inner::Client::instance();
    
    Inner::Client::Channel *channel = new Inner::Client::Channel(client, server);
    
    g_stubs.foo_clt = new FooService::Stub(channel);
    g_stubs.bar_clt = new BarService::Stub(channel);
    g_stubs.baz_clt = new BazService::Stub(channel);
    
    RoutineEnvironment::startCoroutine(test_routine, &g_stubs);
    
    printf("running...\n");
    
    RoutineEnvironment::runEventLoop(100);
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    Inner::Server *server = Inner::Server::create();
    server->registerService(&g_fooService);
    server->registerService(&g_barService);
    server->registerService(&g_bazService);
    
    RoutineEnvironment::startCoroutine(log_routine, NULL);
    std::thread t = std::thread(clientEntry, server);
    
    RoutineEnvironment::runEventLoop();
    
    return 0;
}

