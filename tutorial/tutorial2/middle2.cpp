//
//  main.cpp
//  Tutorial2Middle2
//
//  Created by Xianke Liu on 2018/3/9.
//  Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_routine_env.h"
#include "corpc_client.h"
#include "corpc_server.h"

#include "helloworld.pb.h"

using namespace CoRpc;

class HelloWorldServiceImpl : public HelloWorldService {
public:
    HelloWorldServiceImpl(const std::string& sip, uint32_t sport): _sip(sip), _sport(sport) {}
    virtual void foo(::google::protobuf::RpcController* controller,
                    const ::FooRequest* request,
                    ::FooResponse* response,
                    ::google::protobuf::Closure* done) {
        printf("HelloWorldServiceImpl::foo is called\n");
        
        get_helloworld_clt()->foo(controller, request, response, done);
    }
    
private:
    std::string _sip;
    uint32_t _sport;
    HelloWorldService::Stub *get_helloworld_clt();
    
private:
    static __thread HelloWorldService::Stub *_helloworld_clt;
};

__thread HelloWorldService::Stub *HelloWorldServiceImpl::_helloworld_clt(nullptr);

HelloWorldService::Stub *HelloWorldServiceImpl::get_helloworld_clt() {
    if (!_helloworld_clt) {
        Client *client = Client::instance();
        if (client) {
            Client::Channel *channel = new Client::Channel(client, _sip, _sport, 1);
            
            _helloworld_clt= new HelloWorldService::Stub(channel, ::google::protobuf::Service::STUB_OWNS_CHANNEL);
        }
    }
    
    return _helloworld_clt;
}

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
    
    Server *server = Server::create(false, 1, ip, port);
    
    HelloWorldServiceImpl *helloWorldService = new HelloWorldServiceImpl(sip, sport);
    server->registerService(helloWorldService);
    
    RoutineEnvironment::runEventLoop();
}

