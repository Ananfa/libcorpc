/*
 * Created by Xianke Liu on 2018/4/27.
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
#include "corpc_message_server.h"
#include "corpc_io.h"

#include <signal.h>

#include <google/protobuf/message.h>
#include "echo.pb.h"

static int g_cnt = 0;

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
        
        LOG("time %ld seconds, cnt: %d, average: %d, total: %d\n", difTime, g_cnt, average, total);
        
        g_cnt = 0;
    }
    
    return NULL;
}

int main(int argc, const char * argv[]) {
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
    corpc::IO *io = corpc::IO::create(1, 1);
    
    corpc::UdpMessageServer *server = new corpc::UdpMessageServer(io, true, ip, port);
    server->start();
    
    server->registerMessage(1, new FooRequest, false, [](std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<corpc::Connection> conn) {
        FooRequest * request = static_cast<FooRequest*>(msg.get());
        
        g_cnt++;
        std::shared_ptr<FooResponse> response(new FooResponse);
        
        std::string str = request->text();
        std::string tmp = str;
        for (int i = 1; i < request->times(); i++)
            str += (" " + tmp);
        response->set_text(str);
        
        std::shared_ptr<corpc::SendMessageInfo> sendInfo(new corpc::SendMessageInfo);
        sendInfo->type = 1;
        sendInfo->isRaw = false;
        sendInfo->msg = response;
        
        conn->send(sendInfo);
    });
    
    corpc::RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    corpc::RoutineEnvironment::runEventLoop();
}

