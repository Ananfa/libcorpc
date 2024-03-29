/*
 * Created by Xianke Liu on 2018/3/9.
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

#include "helloworld.pb.h"

using namespace corpc;

static void *helloworld_routine( void *arg )
{
    co_enable_hook_sys();
    
    LOG("rpc_routine begin\n");
    
    HelloWorldService::Stub *helloworld_clt = (HelloWorldService::Stub *)arg;
    
    FooRequest *request = new FooRequest();
    FooResponse *response = new FooResponse();
    Controller *controller = new Controller();
    
    request->set_msg1("Hello");
    request->set_msg2("World");
    
    helloworld_clt->foo(controller, request, response, NULL);
    
    if (controller->Failed()) {
        ERROR_LOG("Rpc Call Failed : error code:%d, error msg:%s\n", ((Controller *)controller)->GetErrorCode(), controller->ErrorText().c_str());
    } else {
        LOG("========= %s =========\n", response->msg().c_str());
    }
    
    delete controller;
    delete response;
    delete request;
    
    return NULL;
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    if(argc<4){
        LOG("Usage:\n"
               "Tutorial1Client [HOST] [PORT] [NUM]\n");
        return -1;
    }
    
    std::string host = argv[1];
    unsigned short int port = atoi(argv[2]);
    uint32_t num = atoi(argv[3]);
    
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigaction( SIGPIPE, &sa, NULL );
    
    IO *io = IO::create(1, 1);
    
    RpcClient *client = RpcClient::create(io);
    RpcClient::Channel *channel = new RpcClient::Channel(client, host, port, 1);
    HelloWorldService::Stub *helloworld_clt = new HelloWorldService::Stub(channel);
    
    for (int i = 0; i < num; i++) {
        RoutineEnvironment::startCoroutine(helloworld_routine, helloworld_clt);
    }
    
    LOG("running...\n");
    
    RoutineEnvironment::runEventLoop();
}
