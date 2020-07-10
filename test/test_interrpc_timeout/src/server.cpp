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
#include "corpc_rpc_server.h"

#include <signal.h>
#include <stdlib.h>

#include "qux.pb.h"

using namespace corpc;

static int iQuxCnt = 0;

static void *log_routine( void *arg )
{
    co_enable_hook_sys();
    
    int total = 0;
    int average = 0;
    
    time_t startAt = time(NULL);
    
    while (true) {
        sleep(1);
        
        total += iQuxCnt;
        
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
        
        LOG("time %ld seconds, qux: %d, average: %d, total: %d\n", difTime, iQuxCnt, average, total);
        
        iQuxCnt = 0;
    }
    
    return NULL;
}

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

        iQuxCnt++;

        int r = rand() % 1050 + 1;
        msleep(r);
    }
};

static QuxServiceImpl g_quxService;

int main(int argc, char *argv[]) {
    co_start_hook();
    
    if(argc<3){
        LOG("Usage:\n"
               "rpcsvr [IP] [PORT]\n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigaction( SIGPIPE, &sa, NULL );
    
    // 注册服务
    IO *io = IO::create(0, 1);
    
    RpcServer *server = RpcServer::create(io, 0, ip, port);
    server->registerService(&g_quxService);
    
    RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop();
}
