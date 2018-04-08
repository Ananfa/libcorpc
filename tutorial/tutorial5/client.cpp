/*
 * Created by Xianke Liu on 2018/3/13.
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
#include "corpc_client.h"
#include "corpc_controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "factorial.pb.h"

using namespace CoRpc;

static void *rpc_routine( void *arg )
{
    co_enable_hook_sys();
    
    printf("rpc_routine begin\n");
    
    Client::Channel *channel = (Client::Channel*)arg;
    
    FactorialService::Stub *factorial_clt = new FactorialService::Stub(channel);
    
    FactorialRequest *request = new FactorialRequest();
    FactorialResponse *response = new FactorialResponse();
    Controller *controller = new Controller();
    
    request->set_n(10);
    
    factorial_clt->factorial(controller, request, response, NULL);
    
    if (controller->Failed()) {
        printf("Rpc Call Failed : %s\n", controller->ErrorText().c_str());
    } else {
        printf("========= %llu =========\n", response->result());
    }
    
    delete controller;
    delete response;
    delete request;
    
    delete factorial_clt;
    
    return NULL;
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    if(argc<3){
        printf("Usage:\n"
               "Tutorial2Client [IP] [PORT]\n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigaction( SIGPIPE, &sa, NULL );
    
    IO* io = IO::create(1, 1);
    
    Client *client = Client::create(io);
    Client::Channel *channel = new Client::Channel(client, ip, port, 1);
    
    RoutineEnvironment::startCoroutine(rpc_routine, channel);
    
    printf("running...\n");
    
    RoutineEnvironment::runEventLoop(100);
}
