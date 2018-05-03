//
//  udpsvr1.cpp
//  testUdp
//
//  Created by Xianke Liu on 2018/4/27.
//Copyright © 2018年 Dena. All rights reserved.
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

class TestUdpServer : public CoRpc::UdpMessageServer {
public:
    static TestUdpServer* create(CoRpc::IO *io, const std::string& ip, uint16_t port);
    
private:
    TestUdpServer( CoRpc::IO *io, const std::string& ip, uint16_t port);
    virtual ~TestUdpServer() {}
};

TestUdpServer::TestUdpServer(CoRpc::IO *io, const std::string& ip, uint16_t port): CoRpc::UdpMessageServer(io, true, ip, port) {
}

TestUdpServer* TestUdpServer::create( CoRpc::IO *io, const std::string& ip, uint16_t port) {
    assert(io);
    TestUdpServer *server = new TestUdpServer(io, ip, port);
    
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
    CoRpc::IO *io = CoRpc::IO::create(1, 1);
    
    TestUdpServer *server = TestUdpServer::create(io, ip, port);
    
    server->registerMessage(1, new FooRequest, false, [](std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<CoRpc::Connection> conn) {
        FooRequest * request = static_cast<FooRequest*>(msg.get());
        
        g_cnt++;
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
    });
    
    CoRpc::RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    CoRpc::RoutineEnvironment::runEventLoop();
}

