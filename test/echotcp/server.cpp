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
#include "echo.pb.h"

static int g_cnt = 0;

static void *log_routine( void *arg )
{
    co_enable_hook_sys();
    
    int total = 0;
    int average = 0;
    
    time_t startAt = time(NULL);
    
    while (true) {
        sleep(1);
        
        total += g_cnt;
        
        if (total == 0) {
            startAt = time(NULL);
            continue;
        }
        
        time_t now = time(NULL);
        
        time_t difTime = now - startAt;
        if (difTime > 0) {
            average = total / difTime;
        } else {
            average = total;
        }
        
        printf("time %ld seconds, cnt: %d, average: %d, total: %d\n", difTime, g_cnt, average, total);
        
        g_cnt = 0;
    }
    
    return NULL;
}

class TestTcpServer : public corpc::TcpMessageServer {
public:
    static TestTcpServer* create(corpc::IO *io, bool needHB, const std::string& ip, uint16_t port);
    
private:
    TestTcpServer( corpc::IO *io, bool needHB, const std::string& ip, uint16_t port);
    virtual ~TestTcpServer() {}
};

TestTcpServer::TestTcpServer(corpc::IO *io, bool needHB, const std::string& ip, uint16_t port): corpc::TcpMessageServer(io, needHB, ip, port) {
}

TestTcpServer* TestTcpServer::create(corpc::IO *io, bool needHB, const std::string& ip, uint16_t port) {
    assert(io);
    TestTcpServer *server = new TestTcpServer(io, needHB, ip, port);
    
    server->start();
    return server;
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
    corpc::IO *io = corpc::IO::create(1, 1);
    
    TestTcpServer *server = TestTcpServer::create(io, false, ip, port);
    
    server->registerMessage(1, new FooRequest, false, [](std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<corpc::Connection> conn) {
        FooRequest * request = static_cast<FooRequest*>(msg.get());
        
        g_cnt++;
        std::shared_ptr<FooResponse> response(new FooResponse);
        
        std::string str = request->text();
        std::string tmp = str;
        for (int i = 1; i < request->times(); i++)
            str += (" " + tmp);
        response->set_text(str);
        
        std::shared_ptr<corpc::SendMessageInfo> sendInfo(new corpc::SendMessageInfo);
        sendInfo->type = 1;
        sendInfo->isRaw = false;
        sendInfo->msg = response;
        
        conn->send(sendInfo);
    });
    
    corpc::RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    corpc::RoutineEnvironment::runEventLoop();
}
