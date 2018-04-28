//
//  main.cpp
//  testtcp
//
//  Created by Xianke Liu on 2018/4/17.
//  Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_routine_env.h"
#include "corpc_message_server.h"
#include "corpc_io.h"

#include <signal.h>

#include <google/protobuf/message.h>
#include "foo.pb.h"

class TestTcpServer : public CoRpc::TcpMessageServer {
public:
    static TestTcpServer* create(CoRpc::IO *io, bool needHB, const std::string& ip, uint16_t port);
    
    static void fooHandle(std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<CoRpc::Connection> conn);
    
private:
    TestTcpServer( CoRpc::IO *io, bool needHB, const std::string& ip, uint16_t port);
    virtual ~TestTcpServer() {}
};

TestTcpServer::TestTcpServer(CoRpc::IO *io, bool needHB, const std::string& ip, uint16_t port): CoRpc::TcpMessageServer(io, needHB, ip, port) {
}

TestTcpServer* TestTcpServer::create(CoRpc::IO *io, bool needHB, const std::string& ip, uint16_t port) {
    assert(io);
    TestTcpServer *server = new TestTcpServer(io, needHB, ip, port);
    
    server->start();
    return server;
}

void TestTcpServer::fooHandle(std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<CoRpc::Connection> conn) {
    FooRequest * request = static_cast<FooRequest*>(msg.get());
    
    std::shared_ptr<FooResponse> response(new FooResponse);
    
    std::string str = request->text();
    std::string tmp = str;
    for (int i = 1; i < request->times(); i++)
        str += (" " + tmp);
    response->set_text(str);
    
    std::shared_ptr<CoRpc::SendMessageInfo> sendInfo(new CoRpc::SendMessageInfo);
    sendInfo->type = 1;
    sendInfo->isRaw = false;
    sendInfo->msg = response;
    
    conn->send(sendInfo);
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    if(argc<3){
        printf("Usage:\n"
               "rpcsvr [IP] [PORT]\n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigaction( SIGPIPE, &sa, NULL );
    
    // 注册服务
    CoRpc::IO *io = CoRpc::IO::create(1, 1);
    
    TestTcpServer *server = TestTcpServer::create(io, false, ip, port);
    
    server->registerMessage(1, new FooRequest, false, TestTcpServer::fooHandle);
    
    CoRpc::RoutineEnvironment::runEventLoop();
}
