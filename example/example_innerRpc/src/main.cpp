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
        
        printf("time %ld seconds, foo:Succ %d Fail %d, bar:Succ %d Fail %d, baz:Succ %d Fail %d Send %d Done %d, average:Succ %d, total: %d\n", difTime, iFooSuccCnt, iFooFailCnt, iBarSuccCnt, iBarFailCnt, iBazSuccCnt, iBazFailCnt, iTotalBazSend, iTotalBazDone, averageSucc, totalSucc + totalFail);
        
        iFooSuccCnt = 0;
        iFooFailCnt = 0;
        iBarSuccCnt = 0;
        iBarFailCnt = 0;
        iBazSuccCnt = 0;
        iBazFailCnt = 0;
    }
    
    return NULL;
}

static void callDoneHandle(::google::protobuf::Message *request, corpc::Controller *controller) {
    if (controller->Failed()) {
        iBazFailCnt++;
    } else {
        iBazSuccCnt++;
    }
    
    iTotalBazDone++;
    
    delete controller;
    delete request;
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
                
                testStubs->baz_clt->Baz(controller, request, NULL, google::protobuf::NewCallback<::google::protobuf::Message *>(&callDoneHandle, request, controller));
                
                iTotalBazSend++;
                
                break;
            }
            default:
                break;
        }
    }
    
    return NULL;
}

int g_routineNum = 100;
static void clientEntry() {
    for (int i=0; i<g_routineNum; i++) {
        RoutineEnvironment::startCoroutine(rpc_routine, &g_stubs);
    }
    
    printf("running...\n");
    
    RoutineEnvironment::runEventLoop();
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    InnerRpcServer *server = InnerRpcServer::create();
    server->registerService(&g_fooService);
    server->registerService(&g_barService);
    server->registerService(&g_bazService);
    
    InnerRpcClient *client = InnerRpcClient::instance();
    
    InnerRpcClient::Channel *channel = new InnerRpcClient::Channel(client, server);
    
    g_stubs.foo_clt = new FooService::Stub(channel);
    g_stubs.bar_clt = new BarService::Stub(channel);
    g_stubs.baz_clt = new BazService::Stub(channel);
    
    // 在主线程中直接开一组rpc_routine协程
    for (int i=0; i<g_routineNum; i++) {
        RoutineEnvironment::startCoroutine(rpc_routine, &g_stubs);
    }
    
    // 新开一线程，并在其中开一组rpc_routine协程
    std::thread t1 = std::thread(clientEntry);
    
    // 再开一线程，并在其中开一组rpc_routine协程
    std::thread t2 = std::thread(clientEntry);
    
    // 注意：线程开多了性能不一定会增加，也可能降低，因此在具体项目中需要根据CPU核心数来调整线程数量
    
    RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop();
    
    return 0;
}
