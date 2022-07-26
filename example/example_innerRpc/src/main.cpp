/*
 * Created by Xianke Liu on 2018/3/2.
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

#include "corpc_routine_env.h"
#include "corpc_controller.h"
#include "corpc_inner_rpc.h"

#include "foo.pb.h"
#include "bar.pb.h"
#include "baz.pb.h"

#include <thread>

using namespace corpc;

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
        
        //LOG("BazServiceImpl::Baz: %s\n", str.c_str());
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

static int iTotalBazSend = 0;
static int iTotalBazDone = 0;

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
        
        LOG("time %ld seconds, foo:Succ %d Fail %d, bar:Succ %d Fail %d, baz:Succ %d Fail %d Send %d Done %d, average:Succ %d, total: %d\n", difTime, iFooSuccCnt, iFooFailCnt, iBarSuccCnt, iBarFailCnt, iBazSuccCnt, iBazFailCnt, iTotalBazSend, iTotalBazDone, averageSucc, totalSucc + totalFail);
        
        iFooSuccCnt = 0;
        iFooFailCnt = 0;
        iBarSuccCnt = 0;
        iBarFailCnt = 0;
        iBazSuccCnt = 0;
        iBazFailCnt = 0;
    }
    
    return NULL;
}

static void callDoneHandle1(::google::protobuf::Message *request, corpc::Controller *controller) {
    if (controller && controller->Failed()) {
        iBazFailCnt++;
    } else {
        iBazSuccCnt++;
    }
    
    iTotalBazDone++;
    
    if (controller) {
        delete controller;
    }
    
    delete request;
}

static void *foo_routine( void *arg )
{
    co_enable_hook_sys();
    
    Test_Stubs *testStubs = (Test_Stubs*)arg;

    while (true) {
        FooRequest *request = new FooRequest();
        FooResponse *response = new FooResponse();
        Controller *controller = new Controller();
        
        request->set_text("Hello Foo");
        request->set_times(1);
        
        do {
            controller->Reset();
            testStubs->foo_clt->Foo(controller, request, response, NULL);
            
            if (controller->Failed()) {
                //LOG("Rpc Call Failed : %s\n", controller->ErrorText().c_str());
                iFooFailCnt++;
                
                msleep(100);
            } else {
                //LOG("++++++ Rpc Response is %s\n", response->text().c_str());
                iFooSuccCnt++;
            }
        } while (controller->Failed());
        
        delete controller;
        delete response;
        delete request;
    }

    return NULL;
}

static void *bar_routine( void *arg )
{
    co_enable_hook_sys();
    
    Test_Stubs *testStubs = (Test_Stubs*)arg;

    while (true) {
        BarRequest *request = new BarRequest();
        BarResponse *response = new BarResponse();
        Controller *controller = new Controller();
        
        request->set_text("Hello Bar");
        request->set_times(1);
        
        do {
            controller->Reset();
            testStubs->bar_clt->Bar(controller, request, response, NULL);
            
            if (controller->Failed()) {
                //LOG("Rpc Call Failed : %s\n", controller->ErrorText().c_str());
                iBarFailCnt++;
                
                msleep(100);
            } else {
                //LOG("++++++ Rpc Response is %s\n", response->text().c_str());
                iBarSuccCnt++;
            }
        } while (controller->Failed());
        
        delete controller;
        delete response;
        delete request;
    }

    return NULL;
}

static void *baz_routine( void *arg )
{
    co_enable_hook_sys();
    
    Test_Stubs *testStubs = (Test_Stubs*)arg;

    while (true) {
        BazRequest *request = new BazRequest();
        Controller *controller = nullptr;//new Controller();
        
        request->set_text("Hello Baz");
        
        // not_care_response类型的rpc实际上是单向消息传递，不关心成功与否，一般用于GameServer和GatewayServer之间的消息传递。
        // 注意：not_care_response类型的rpc调用是异步的，request和controller对象在回调处理中才能删除，不能在调用语句后面马上删除。
        // 因此not_care_response类型的rpc调用必须提供回调对象
        testStubs->baz_clt->Baz(controller, request, NULL, google::protobuf::NewCallback<::google::protobuf::Message *>(&callDoneHandle1, request, controller));
        
        iTotalBazSend++;
        
        // 注意：这里sleep会影响性能测试
        //if (iTotalBazSend % 10 == 0) {
            msleep(1);
        //}
    }

    return NULL;
}

int foo_routine_num = 1000;
int bar_routine_num = 1000;
int baz_routine_num = 1000;
static void *test_routine( void *arg )
{
    co_enable_hook_sys();
    
    LOG("test_routine begin\n");
    
    for (int i=0; i<foo_routine_num; i++) {
        RoutineEnvironment::startCoroutine(foo_routine, arg);
    }
    
    for (int i=0; i<bar_routine_num; i++) {
        RoutineEnvironment::startCoroutine(bar_routine, arg);
    }
    
    for (int i=0; i<baz_routine_num; i++) {
        RoutineEnvironment::startCoroutine(baz_routine, arg);
    }
    
    return NULL;
}

void clientThread() {
    RoutineEnvironment::startCoroutine(test_routine, &g_stubs);
    
    LOG("thread %d running...\n", GetPid());
    
    RoutineEnvironment::runEventLoop();
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    if(argc<4){
        LOG("Usage:\n"
               "innerRpc [FOO_ROUTINE_NUM] [BAR_ROUTINE_NUM] [BAZ_ROUTINE_NUM]\n");
        return -1;
    }

    foo_routine_num = atoi( argv[1] );
    bar_routine_num = atoi( argv[2] );
    baz_routine_num = atoi( argv[3] );

    InnerRpcServer *server = new InnerRpcServer();
    server->registerService(&g_fooService);
    server->registerService(&g_barService);
    server->registerService(&g_bazService);
    server->start(4);
    
    InnerRpcChannel *channel = new InnerRpcChannel(server);
    
    g_stubs.foo_clt = new FooService::Stub(channel);
    g_stubs.bar_clt = new BarService::Stub(channel);
    g_stubs.baz_clt = new BazService::Stub(channel);
    
    // 在主线程中直接开test_routine协程
    RoutineEnvironment::startCoroutine(test_routine, &g_stubs);
    
    // 新开一线程，并在其中开一组rpc_routine协程
    std::thread t1 = std::thread(clientThread);
    
    // 再开一线程，并在其中开一组rpc_routine协程
    std::thread t2 = std::thread(clientThread);
    
    // 注意：线程开多了性能不一定会增加，也可能降低，因此在具体项目中需要根据CPU核心数来调整线程数量
    
    RoutineEnvironment::startCoroutine(log_routine, NULL);
    LOG("innerRpc started\n");
    RoutineEnvironment::runEventLoop();
    
    return 0;
}
