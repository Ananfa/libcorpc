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
#include "echo.pb.h"

using namespace corpc;

static void *helloworld_routine( void *arg )
{
    co_enable_hook_sys();
    
    printf("rpc_routine begin\n");
    
    RpcClient::Channel *channel = (RpcClient::Channel*)arg;
    
    HelloWorldService::Stub *helloworld_clt = new HelloWorldService::Stub(channel);
    
    FooRequest *request = new FooRequest();
    FooResponse *response = new FooResponse();
    Controller *controller = new Controller();
    
    request->set_msg1("Hello");
    request->set_msg2("World");
    
    helloworld_clt->foo(controller, request, response, NULL);
    
    if (controller->Failed()) {
        printf("Rpc Call Failed : %s\n", controller->ErrorText().c_str());
    } else {
        printf("========= %s =========\n", response->msg().c_str());
    }
    
    delete controller;
    delete response;
    delete request;
    
    delete helloworld_clt;
    
    return NULL;
}

static void *echo_routine( void *arg )
{
    co_enable_hook_sys();
    
    printf("echo_routine begin\n");
    
    RpcClient::Channel *channel = (RpcClient::Channel*)arg;
    
    EchoService::Stub *echo_clt = new EchoService::Stub(channel);
    
    EchoRequest *request = new EchoRequest();
    EchoResponse *response = new EchoResponse();
    Controller *controller = new Controller();
    
    request->set_msg("Echo");
    request->set_seconds(5);
    
    echo_clt->echo(controller, request, response, NULL);
    
    if (controller->Failed()) {
        printf("Rpc Call Failed : %s\n", controller->ErrorText().c_str());
    } else {
        printf("========= %s =========\n", response->msg().c_str());
    }
    
    delete controller;
    delete response;
    delete request;
    
    delete echo_clt;
    
    return NULL;
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    if(argc<3){
        printf("Usage:\n"
               "Tutorial1Client [HOST] [PORT]\n");
        return -1;
    }
    
    std::string host = argv[1];
    unsigned short int port = atoi(argv[2]);
    
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigaction( SIGPIPE, &sa, NULL );
    
    IO *io = IO::create(1, 1);
    
    RpcClient *client = RpcClient::create(io);
    RpcClient::Channel *channel = new RpcClient::Channel(client, host, port, 1);
    
    RoutineEnvironment::startCoroutine(echo_routine, channel);
    RoutineEnvironment::startCoroutine(helloworld_routine, channel);
    
    printf("running...\n");
    
    RoutineEnvironment::runEventLoop(100);
}
