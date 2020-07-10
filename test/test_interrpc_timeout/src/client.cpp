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

#include "qux.pb.h"

using namespace corpc;

static int iQuxSuccCnt = 0;
static int iQuxFailCnt = 0;

static int iTotalQuxSend = 0;
static int iTotalQuxResp = 0;

static void *log_routine( void *arg )
{
    co_enable_hook_sys();
    
    int totalSucc = 0;
    int totalFail = 0;
    int averageSucc = 0;
    
    time_t startAt = time(NULL);
    
    while (true) {
        sleep(1);
        
        totalSucc += iQuxSuccCnt;
        totalFail += iQuxFailCnt;
        
        time_t now = time(NULL);
        
        time_t difTime = now - startAt;
        if (difTime > 0) {
            averageSucc = totalSucc / difTime;
        } else {
            averageSucc = totalSucc;
        }
        
        LOG("time %ld seconds, qux:Succ %d Fail %d Send %d Resp %d, average:Succ %d, total: %d\n", difTime, iQuxSuccCnt, iQuxFailCnt, iTotalQuxSend, iTotalQuxResp, averageSucc, totalSucc + totalFail);
        
        iQuxSuccCnt = 0;
        iQuxFailCnt = 0;
    }
    
    return NULL;
}

static void *qux_routine( void *arg )
{
    co_enable_hook_sys();
    
    QuxService::Stub *stub = (QuxService::Stub*)arg;

    while (true) {
        QuxRequest *request = new QuxRequest();
        QuxResponse *response = new QuxResponse();
        Controller *controller = new Controller();
        
        request->set_text("Hello Qux");
        request->set_times(1);
        
        iTotalQuxSend++;
        stub->Qux(controller, request, response, NULL);
        iTotalQuxResp++;
        
        if (controller->Failed()) {
            //LOG("Rpc Call Failed : %s\n", controller->ErrorText().c_str());
            iQuxFailCnt++;
        } else {
            //LOG("++++++ Rpc Response is %s\n", response->text().c_str());
            iQuxSuccCnt++;
        }
        
        delete controller;
        delete response;
        delete request;
    }

    return NULL;
}

int qux_routine_num = 1000;

std::string host;
unsigned short int port;

IO *io;
RpcClient *client;
QuxService::Stub *qux_clt;

static void *test_routine( void *arg )
{
    co_enable_hook_sys();
    
    LOG("test_routine begin\n");
    
    for (int i=0; i<qux_routine_num; i++) {
        RoutineEnvironment::startCoroutine(qux_routine, qux_clt);
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
    qux_clt = new QuxService::Stub(new RpcClient::Channel(client, host, port, 1), google::protobuf::Service::STUB_OWNS_CHANNEL);

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
