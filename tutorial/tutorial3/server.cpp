//
//  main.cpp
//  Tutorial3Server
//
//  Created by Xianke Liu on 2018/3/9.
//  Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_routine_env.h"
#include "corpc_server.h"

#include "helloworld.pb.h"
#include "echo.pb.h"

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

class EchoServiceImpl : public EchoService {
public:
    EchoServiceImpl() {}
    virtual void echo(::google::protobuf::RpcController* controller,
                     const ::EchoRequest* request,
                     ::EchoResponse* response,
                     ::google::protobuf::Closure* done) {
        printf("EchoServiceImpl::echo is called\n");
        
        std::string msg = request->msg();
        uint32_t seconds = request->seconds();
        
        sleep(seconds);
        
        response->set_msg(msg);
    }
};

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    if(argc<3){
        printf("Usage:\n"
               "Tutorial1Server [IP] [PORT]\n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    
    // 注册服务
    IO *io = IO::create(1,1);
    assert(io);
    
    io->start();
    
    Server *server = new Server(io, false, 0, ip, port);
    
    HelloWorldServiceImpl *helloWorldService = new HelloWorldServiceImpl();
    EchoServiceImpl *echoService = new EchoServiceImpl();
    server->registerService(helloWorldService);
    server->registerService(echoService);
    
    server->start();
    
    RoutineEnvironment::runEventLoop();
}
