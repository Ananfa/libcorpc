//
//  testrpc_cli.cpp
//  rpccli
//
//  Created by Xianke Liu on 2017/11/2.
//  Copyright © 2017年 Dena. All rights reserved.
//

#include "corpc_routine_env.h"
#include "corpc_controller.h"
#include "corpc_server.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include <list>

#include "foo.pb.h"
#include "bar.pb.h"
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>

using namespace CoRpc;

class FooServiceImpl : public FooService {
public:
    FooServiceImpl() {}
    virtual void Foo(::google::protobuf::RpcController* controller,
                     const ::FooRequest* request,
                     ::FooResponse* response,
                     ::google::protobuf::Closure* done) {
        std::string str = request->text();
        std::string tmp = str;
        for (int i = 1; i < request->times(); i++)
            str += (" " + tmp);
        response->set_text(str);
        response->set_result(true);
    }
};

class BarServiceImpl : public BarService {
public:
    BarServiceImpl() {}
    virtual void Bar(::google::protobuf::RpcController* controller,
                     const ::BarRequest* request,
                     ::BarResponse* response,
                     ::google::protobuf::Closure* done) {
        std::string str = request->text();
        std::string tmp = str;
        for (int i = 1; i < request->times(); i++)
            str += (" " + tmp);
        response->set_text(str);
        response->set_result(true);
    }
};

static FooServiceImpl g_fooService;
static BarServiceImpl g_barService;

int main(int argc, char *argv[]) {
    if(argc<3){
        printf("Usage:\n"
               "rpcsvr [IP] [PORT]\n");
        return -1;
    }
    
    //char *ip = argv[1];
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    
    start_hook();
    
    // 注册服务
    IO *io = new IO(2,2);
    Server *server = new Server(io, true, 2, ip, port);
    server->registerService(&g_fooService);
    server->registerService(&g_barService);
    
    if (io->start() && server->start()) {
        RoutineEnvironment::runEventLoop();
    }
}
