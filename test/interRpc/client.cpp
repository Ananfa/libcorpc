/*
 * Created by Xianke Liu on 2017/11/2.
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
#include "corpc_rpc_client.h"
#include "corpc_controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "foo.pb.h"
#include "bar.pb.h"
#include "baz.pb.h"

using namespace CoRpc;

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

int test_routine_count = 1000;
static void *test_routine( void *arg )
{
    co_enable_hook_sys();
    
    printf("test_routine begin\n");
    
    for (int i=0; i<test_routine_count; i++) {
        RoutineEnvironment::startCoroutine(rpc_routine, arg);
    }
    
    return NULL;
}

int main(int argc, char *argv[]) {
    co_start_hook();
    
    if(argc<4){
        printf("Usage:\n"
               "rpccli [IP] [PORT] [TEST_ROUTINE_COUNT]\n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    test_routine_count = atoi( argv[3] );
    
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigaction( SIGPIPE, &sa, NULL );
    
    IO *io = IO::create(0, 1);
    
    RpcClient *client = RpcClient::create(io);
    RpcClient::Channel *channel = new RpcClient::Channel(client, ip, port, 1);
    
    g_stubs.foo_clt = new FooService::Stub(channel);
    g_stubs.bar_clt = new BarService::Stub(channel);
    g_stubs.baz_clt = new BazService::Stub(channel);
    
    RoutineEnvironment::startCoroutine(log_routine, NULL);
    RoutineEnvironment::startCoroutine(test_routine, &g_stubs);
    
    printf("running...\n");
    
    RoutineEnvironment::runEventLoop(100);
}
