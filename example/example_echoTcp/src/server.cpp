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
#include <stdlib.h>

#include <google/protobuf/message.h>
#include "corpc_crypter.h"
#include "echo.pb.h"

static int g_cnt = 0;
static int g_bcnt = 0;

static void *log_routine( void *arg )
{
    co_enable_hook_sys();
    
    int total = 0;
    int average = 0;
    
    time_t startAt = time(NULL);
    
    while (true) {
        sleep(1);
        
        total += g_cnt + g_bcnt;
        
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
        
        LOG("time %ld seconds, cnt: %d, bcnt: %d, average: %d, total: %d\n", difTime, g_cnt, g_bcnt, average, total);
        
        g_cnt = 0;
        g_bcnt = 0;
    }
    
    return NULL;
}

static void *ban_routine( void *arg )
{
    co_enable_hook_sys();

    corpc::MessageTerminal *terminal = (corpc::MessageTerminal*)arg;

    bool banned = false;
    while (true) {
        sleep(5);

        std::list<int> banMsgs;
        if (banned) {
            LOG("unban message\n");
        } else {
            LOG("ban message\n");
            banMsgs.push_back(1);
        }

        terminal->setBanMessages(banMsgs);
        banned = !banned;
    }

    return NULL;
}

void sigintHandler( int signum ) {
    LOG("Interrupt signal (%d) received.\n", signum);

    // cleanup and close up stuff here  
    // terminate program  

    exit(signum);
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

    signal(SIGINT, sigintHandler);

    std::string key = "1234567fvxcvc";
    std::shared_ptr<corpc::Crypter> crypter = std::shared_ptr<corpc::Crypter>(new corpc::SimpleXORCrypter(key));

    // 注册服务
    corpc::IO *io = corpc::IO::create(1, 1, 0);

    corpc::MessageTerminal *terminal = new corpc::MessageTerminal(true, true, true, true);
    
    terminal->registerMessage(CORPC_MSG_TYPE_CONNECT, nullptr, false, [&crypter](int16_t type, uint16_t tag, std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<corpc::MessageTerminal::Connection> conn) {
        LOG("connect %d\n", conn->getfd());
        conn->setCrypter(crypter);
        std::shared_ptr<corpc::MessageBuffer> msgBuffer(new corpc::MessageBuffer(true));
        conn->setMsgBuffer(msgBuffer);

        std::shared_ptr<ServerReady> readyMsg(new ServerReady);
        readyMsg->set_status(1);
        conn->send(3, false, true, true, 0, readyMsg);
    });

    terminal->registerMessage(CORPC_MSG_TYPE_CLOSE, nullptr, false, [](int16_t type, uint16_t tag, std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<corpc::MessageTerminal::Connection> conn) {
        LOG("connect %d close\n", conn->getfd());
    });

    terminal->registerMessage(CORPC_MSG_TYPE_BANNED, nullptr, false, [](int16_t type, uint16_t tag, std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<corpc::MessageTerminal::Connection> conn) {
        g_bcnt++;
        std::shared_ptr<BanResponse> response(new BanResponse);
        response->set_type(type);

        conn->send(2, false, true, true, tag, response);
    });

    terminal->registerMessage(1, new FooRequest, false, [](int16_t type, uint16_t tag, std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<corpc::MessageTerminal::Connection> conn) {
        FooRequest * request = static_cast<FooRequest*>(msg.get());
        
        g_cnt++;
        std::shared_ptr<FooResponse> response(new FooResponse);
        
        std::string str = request->text();
        std::string tmp = str;
        for (int i = 1; i < request->times(); i++)
            str += (" " + tmp);
        response->set_text(str);

        conn->send(1, false, true, true, tag, response);
    });

    corpc::TcpMessageServer *server = new corpc::TcpMessageServer(io, nullptr, terminal, ip, port);
    server->start();
    
    corpc::RoutineEnvironment::startCoroutine(log_routine, NULL);
    corpc::RoutineEnvironment::startCoroutine(ban_routine, terminal);
    
    corpc::RoutineEnvironment::runEventLoop();
}
