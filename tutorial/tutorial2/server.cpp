//
//  main.cpp
//  Tutorial2Server
//
//  Created by Xianke Liu on 2018/3/9.
//  Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_routine_env.h"
#include "corpc_server.h"

#include "helloworld.pb.h"

using namespace CoRpc;

class HelloWorldServiceImpl : public HelloWorldService {
public:
    HelloWorldServiceImpl() {}
    virtual void foo(::google::protobuf::RpcController* controller,
                    const ::FooRequest* request,
                    ::FooResponse* response,
                    ::google::protobuf::Closure* done) {
        printf("HelloWorldServiceImpl::foo is called\n");
        
        std::string msg1 = request->msg1();
        std::string msg2 = request->msg2();
        
        response->set_msg(msg1 + " " + msg2);
    }
};

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    if(argc<3){
        printf("Usage:\n"
               "Tutorial2Server [IP] [PORT]\n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    
    IO::initialize(1, 1);
    
    Server *server = Server::create(false, 0, ip, port);
    
    HelloWorldServiceImpl *helloWorldService = new HelloWorldServiceImpl();
    server->registerService(helloWorldService);
    
    RoutineEnvironment::runEventLoop();
}

