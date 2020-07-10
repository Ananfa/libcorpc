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

#include "corpc_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "foo.pb.h"
#include "bar.pb.h"
#include "baz.pb.h"

using namespace corpc;

static int iFooSuccCnt = 0;
static int iFooFailCnt = 0;
static int iBarSuccCnt = 0;
static int iBarFailCnt = 0;
static int iBazSuccCnt = 0;
static int iBazFailCnt = 0;

static int iTotalBazSend = 0;
static int iTotalBazDone = 0;

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

static void *foo_routine( void *arg )
{
    co_enable_hook_sys();
    
    FooService::Stub *stub = (FooService::Stub*)arg;

    FooRequest *request = new FooRequest();
    FooResponse *response = new FooResponse();
    Controller *controller = new Controller();
    
    request->set_text("Hello Foo");
    request->set_times(1);
    stub->Foo(controller, request, response, NULL);
    
    if (controller->Failed()) {
        //LOG("Rpc Call Failed : %s\n", controller->ErrorText().c_str());
        iFooFailCnt++;
        
        msleep(100);
    } else {
        //LOG("++++++ Rpc Response is %s\n", response->text().c_str());
        iFooSuccCnt++;
    }
    
    delete controller;
    delete response;
    delete request;

    delete stub;

    return NULL;
}

static void *bar_routine( void *arg )
{
    co_enable_hook_sys();
    
    BarService::Stub *stub = (BarService::Stub*)arg;

    BarRequest *request = new BarRequest();
    BarResponse *response = new BarResponse();
    Controller *controller = new Controller();
    
    request->set_text("Hello Bar");
    request->set_times(1);
    
    controller->Reset();
    stub->Bar(controller, request, response, NULL);
    
    if (controller->Failed()) {
        //LOG("Rpc Call Failed : %s\n", controller->ErrorText().c_str());
        iBarFailCnt++;
        
        msleep(100);
    } else {
        //LOG("++++++ Rpc Response is %s\n", response->text().c_str());
        iBarSuccCnt++;
    }
    
    delete controller;
    delete response;
    delete request;

    delete stub;

    return NULL;
}

static void *baz_routine( void *arg )
{
    co_enable_hook_sys();
    
    BazService::Stub *stub = (BazService::Stub*)arg;

    BazRequest *request = new BazRequest();
    Controller *controller = new Controller();
    
    request->set_text("Hello Baz");
    
    // not_care_response类型的rpc实际上是单向消息传递，不关心成功与否，一般用于GameServer和GatewayServer之间的消息传递。
    // 注意：not_care_response类型的rpc调用是异步的，request和controller对象在回调处理中才能删除，不能在调用语句后面马上删除。
    // 因此not_care_response类型的rpc调用必须提供回调对象
    stub->Baz(controller, request, NULL, google::protobuf::NewCallback<::google::protobuf::Message *>(&callDoneHandle, request, controller));
    
    iTotalBazSend++;
        
    delete stub;

    return NULL;
}

int foo_routine_num = 1000;
int bar_routine_num = 1000;
int baz_routine_num = 1000;

std::string host;
unsigned short int port;

IO *io;
RpcClient *client;

static void *test_routine( void *arg )
{
    co_enable_hook_sys();
    
    LOG("test_routine begin\n");
    
    int i = 0;
    while (true) {
        FooService::Stub *foo_clt = new FooService::Stub(new RpcClient::Channel(client, host, port, 1), google::protobuf::Service::STUB_OWNS_CHANNEL);
        BarService::Stub *bar_clt = new BarService::Stub(new RpcClient::Channel(static_cast<RpcClient::Channel&>(*foo_clt->channel())), google::protobuf::Service::STUB_OWNS_CHANNEL);
        BazService::Stub *baz_clt = new BazService::Stub(new RpcClient::Channel(static_cast<RpcClient::Channel&>(*foo_clt->channel())), google::protobuf::Service::STUB_OWNS_CHANNEL);
        
        RoutineEnvironment::startCoroutine(foo_routine, foo_clt);
        RoutineEnvironment::startCoroutine(bar_routine, bar_clt);
        RoutineEnvironment::startCoroutine(baz_routine, baz_clt);

        i++;
        if (i % 2 == 0) {
            msleep(1);
            i = 0;
        }
    }

    
    return NULL;
}

void clientThread() {
    RoutineEnvironment::startCoroutine(test_routine, NULL);
    
    LOG("thread %d running...\n", GetPid());
    
    RoutineEnvironment::runEventLoop();
}


int main(int argc, char *argv[]) {
    co_start_hook();
    
    if(argc<3){
        LOG("Usage:\n"
               "rpccli [HOST] [PORT]\n");
        return -1;
    }

    host = argv[1];
    port = atoi(argv[2]);
    
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigaction( SIGPIPE, &sa, NULL );
    
    io = IO::create(0, 1);
    client = RpcClient::create(io);

    // 在主线程中直接开test_routine协程
    RoutineEnvironment::startCoroutine(test_routine, NULL);
    
    // 新开一线程，并在其中开test_routine协程
    //std::thread t1 = std::thread(clientThread);
    
    // 再开一线程
    //std::thread t2 = std::thread(clientThread);
    
    // 注意：线程开多了性能不一定会增加，也可能降低，因此在具体项目中需要根据CPU核心数来调整线程数量
    
    RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    LOG("thread %d running...\n", GetPid());
    
    RoutineEnvironment::runEventLoop();
}
