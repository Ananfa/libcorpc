//
//  testrpc_svr.cpp
//  rpcsvr
//
//  Created by Xianke Liu on 2017/11/2.
//  Copyright © 2017年 Dena. All rights reserved.
//

#include "corpc_routine_env.h"
#include "corpc_server.h"

#include "foo.pb.h"
#include "bar.pb.h"
#include "baz.pb.h"

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

class BazServiceImpl : public BazService {
public:
    BazServiceImpl() {}
    virtual void Baz(::google::protobuf::RpcController* controller,
                     const ::BazRequest* request,
                     ::corpc::Void* response,
                     ::google::protobuf::Closure* done) {
        std::string str = request->text();
        
        //printf("BazServiceImpl::Baz: %s\n", str.c_str());
    }
};

static FooServiceImpl g_fooService;
static BarServiceImpl g_barService;
static BazServiceImpl g_bazService;

int main(int argc, char *argv[]) {
    co_start_hook();
    
    if(argc<3){
        printf("Usage:\n"
               "rpcsvr [IP] [PORT]\n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    
    // 注册服务
    IO *io = IO::create(1,1);
    assert(io);
    Server *server = new Server(io, true, 1, ip, port);
    server->registerService(&g_fooService);
    server->registerService(&g_barService);
    server->registerService(&g_bazService);
    
    if (io->start() && server->start()) {
        RoutineEnvironment::runEventLoop();
    }
}