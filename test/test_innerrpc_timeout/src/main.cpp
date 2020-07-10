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

#include "qux.pb.h"

#include <thread>

using namespace corpc;

class QuxServiceImpl : public QuxService {
public:
    QuxServiceImpl() {}
    virtual void Qux(::google::protobuf::RpcController* controller,
                     const ::QuxRequest* request,
                     ::QuxResponse* response,
                     ::google::protobuf::Closure* done) {
        std::string str = request->text();
        std::string tmp = str;
        for (int i = 1; i < request->times(); i++)
            str += (" " + tmp);
        response->set_text(str);
        response->set_result(true);

        int r = rand() % 1050 + 1;
        msleep(r);
    }
};

static QuxServiceImpl g_quxService;

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

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    if(argc<2){
        LOG("Usage:\n"
               "test [QUX_ROUTINE_NUM]\n");
        return -1;
    }

    qux_routine_num = atoi( argv[1] );

    InnerRpcServer *server = InnerRpcServer::create();
    server->registerService(&g_quxService);
        
    qux_clt = new QuxService::Stub(new InnerRpcChannel(server), google::protobuf::Service::STUB_OWNS_CHANNEL);
    
    // 在主线程中直接开test_routine协程
    RoutineEnvironment::startCoroutine(test_routine, NULL);
    
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
