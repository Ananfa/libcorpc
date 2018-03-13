//
//  main.cpp
//  Tutorial2Middle
//
//  Created by Xianke Liu on 2018/3/9.
//  Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_routine_env.h"
#include "corpc_client.h"
//#include "corpc_controller.h"
#include "corpc_server.h"

#include "helloworld.pb.h"

using namespace CoRpc;

class HelloWorldServiceImpl : public HelloWorldService {
public:
    HelloWorldServiceImpl(HelloWorldService::Stub *helloworld_clt): _helloworld_clt(helloworld_clt) {}
    virtual void foo(::google::protobuf::RpcController* controller,
                    const ::FooRequest* request,
                    ::FooResponse* response,
                    ::google::protobuf::Closure* done) {
        printf("HelloWorldServiceImpl::foo is called\n");
        
        _helloworld_clt->foo(controller, request, response, done);
    }
    
private:
    HelloWorldService::Stub *_helloworld_clt;
};

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    if(argc<5){
        printf("Usage:\n"
               "Tutorial2Middle [IP] [PORT] [ServerIP] [ServerPort] \n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    std::string sip = argv[3];
    unsigned short int sport = atoi(argv[4]);
    
    // 注册服务
    IO::initialize(1, 1);
    
    Client *client = Client::create();
    Client::Channel *channel = new Client::Channel(client, sip, sport, 1);
    
    Server *server = Server::create(false, 0, ip, port);
    
    HelloWorldServiceImpl *helloWorldService = new HelloWorldServiceImpl(new HelloWorldService::Stub(channel));
    server->registerService(helloWorldService);
    
    RoutineEnvironment::runEventLoop();
}

