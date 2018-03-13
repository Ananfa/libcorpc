//
//  main.cpp
//  Tutorial1Client
//
//  Created by Xianke Liu on 2018/3/9.
//  Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_routine_env.h"
#include "corpc_client.h"
#include "corpc_controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "helloworld.pb.h"

using namespace CoRpc;

static void *rpc_routine( void *arg )
{
    co_enable_hook_sys();
    
    printf("rpc_routine begin\n");
    
    Client::Channel *channel = (Client::Channel*)arg;
    
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

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    if(argc<3){
        printf("Usage:\n"
               "Tutorial1Client [IP] [PORT]\n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigaction( SIGPIPE, &sa, NULL );
    
    IO::initialize(1, 1);
    
    Client *client = Client::instance();
    Client::Channel *channel = new Client::Channel(client, ip, port, 1);
    
    RoutineEnvironment::startCoroutine(rpc_routine, channel);
    
    printf("running...\n");
    
    RoutineEnvironment::runEventLoop(100);
}
