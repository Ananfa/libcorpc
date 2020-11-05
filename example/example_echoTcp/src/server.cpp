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

#include <assert.h>
#include <signal.h>
#include <map>

#include <google/protobuf/message.h>
#include "corpc_crypter.h"
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
        
        LOG("time %ld seconds, cnt: %d, average: %d, total: %d\n", difTime, g_cnt, average, total);
        
        g_cnt = 0;
    }
    
    return NULL;
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    if(argc<3){
        LOG("Usage:\n"
               "rpcsvr [IP] [PORT]\n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigaction( SIGPIPE, &sa, NULL );

    std::string key = "1234567fvxcvc";
    corpc::Crypter *crypter = new corpc::SimpleXORCrypter(key);

    std::map<int, uint16_t> clients;
    
    // 注册服务
    corpc::IO *io = corpc::IO::create(1, 1);
    
    corpc::TcpMessageServer *server = new corpc::TcpMessageServer(io, true, true, true, true, ip, port);
    server->start();
    
    server->registerMessage(CORPC_MSG_TYPE_CONNECT, nullptr, false, [crypter, &clients](std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<corpc::MessageServer::Connection> conn) {
        conn->setCrypter(crypter);
        int fd = conn->getfd();
        assert(clients.find(fd) == clients.end());
        clients[fd] = 0;
    });

    server->registerMessage(CORPC_MSG_TYPE_CLOSE, nullptr, false, [&clients](std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<corpc::MessageServer::Connection> conn) {
        int fd = conn->getfd();
        assert(clients.find(fd) != clients.end());
        clients.erase(fd);
    });

    server->registerMessage(1, new FooRequest, false, [&clients](std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<corpc::MessageServer::Connection> conn) {
        FooRequest * request = static_cast<FooRequest*>(msg.get());
        
        g_cnt++;
        std::shared_ptr<FooResponse> response(new FooResponse);
        
        std::string str = request->text();
        std::string tmp = str;
        for (int i = 1; i < request->times(); i++)
            str += (" " + tmp);
        response->set_text(str);

        int fd = conn->getfd();
        assert(clients.find(fd) != clients.end());
        
        conn->send(1, false, true, ++clients[fd], response);
    });
    
    corpc::RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    corpc::RoutineEnvironment::runEventLoop();
}
