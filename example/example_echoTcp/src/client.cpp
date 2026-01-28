//
//  client1.cpp
//  echoTcp
//
//  Created by Xianke Liu on 2018/5/4.
//Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_routine_env.h"
#include "corpc_message_client.h"
#include "echo.pb.h"
#include <signal.h>
#include <thread>

using namespace corpc;

#ifdef NEW_MESSAGE_CLIENT_IMPLEMENT

int main(int argc, const char * argv[])
{
    co_start_hook();

    if(argc<3){
        printf("Usage:\n"
               "echoUdpclt [HOST] [PORT]\n");
        return -1;
    }
    
    uint16_t sendTag = 0;

    std::string host = argv[1];
    uint16_t port = atoi(argv[2]);
    
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigaction( SIGPIPE, &sa, NULL );

    std::string key = "1234567fvxcvc";
    std::shared_ptr<corpc::Crypter> crypter = std::shared_ptr<corpc::Crypter>(new corpc::SimpleXORCrypter(key));

    // 注册服务
    corpc::IO *io = corpc::IO::create(1, 1, 0);

    bool needBuff = true;
    corpc::MessageTerminal *terminal = new corpc::MessageTerminal(true, true, true, needBuff);

    corpc::TcpClient *client = new corpc::TcpClient(io, nullptr, terminal, host, port);
    
    terminal->registerMessage(CORPC_MSG_TYPE_CONNECT, nullptr, false, [&crypter, needBuff](int16_t type, uint16_t tag, std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<corpc::MessageTerminal::Connection> conn) {
        LOG("connect %d\n", conn->getfd());
        conn->setCrypter(crypter);

        if (needBuff) {
            std::shared_ptr<corpc::MessageBuffer> msgBuffer(new corpc::MessageBuffer(1000));
            conn->setMsgBuffer(msgBuffer);
        }
        
        std::shared_ptr<ServerReady> readyMsg(new ServerReady);
        readyMsg->set_status(1);
        conn->send(3, false, true, true, 0, readyMsg);
    });

    terminal->registerMessage(CORPC_MSG_TYPE_CLOSE, nullptr, false, [&](int16_t type, uint16_t tag, std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<corpc::MessageTerminal::Connection> conn) {
        LOG("connect %d close\n", conn->getfd());

        delete client;
        client = new corpc::TcpClient(io, nullptr, terminal, host, port);
        while (!client->connect()) {
            LOG("client reconnect\n");
            sleep(1);
        }
    });

    terminal->registerMessage(1, new FooResponse, false, [&sendTag](int16_t type, uint16_t tag, std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<corpc::MessageTerminal::Connection> conn) {
        FooResponse * response = static_cast<FooResponse*>(msg.get());
        //printf("FooResponse tag:%d -- %s\n", tag, response->text().c_str());

        if (sendTag != tag) {
            ERROR_LOG("tag not match %d != %d\n", sendTag, tag);
        }
        
        // send FooRequest
        std::shared_ptr<FooRequest> request(new FooRequest);
        request->set_text("hello world!");
        request->set_times(10);
        
        conn->send(1, false, true, true, ++sendTag, std::static_pointer_cast<google::protobuf::Message>(request));
    });

    terminal->registerMessage(2, new BanResponse, false, [&sendTag](int16_t type, uint16_t tag, std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<corpc::MessageTerminal::Connection> conn) {
        BanResponse * response = static_cast<BanResponse*>(msg.get());
        //printf("BanResponse tag:%d\n", tag);

        if (sendTag != tag) {
            ERROR_LOG("tag not match %d != %d\n", sendTag, tag);
        }
        
        conn->close();
        // send FooRequest
        //std::shared_ptr<FooRequest> request(new FooRequest);
        //request->set_text("hello world!");
        //request->set_times(10);
        //
        //conn->send(1, false, true, true, ++sendTag, std::static_pointer_cast<google::protobuf::Message>(request));
    });

    terminal->registerMessage(3, new ServerReady, false, [&sendTag](int16_t type, uint16_t tag, std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<corpc::MessageTerminal::Connection> conn) {
        printf("ServerReady tag:%d\n", tag);
        
        // send FooRequest
        std::shared_ptr<FooRequest> request(new FooRequest);
        request->set_text("hello world!");
        request->set_times(10);
        
        conn->send(1, false, true, true, ++sendTag, std::static_pointer_cast<google::protobuf::Message>(request));
    });

    client->connect();

    RoutineEnvironment::runEventLoop();
    return 0;
}


#else

struct Address {
    std::string host;
    uint16_t port;
};

void *testRoutine(void *arg) {
    Address *serverAddr = (Address *)arg;
    std::string host = serverAddr->host;
    uint16_t port = serverAddr->port;
    delete serverAddr;

    std::string key("1234567fvxcvc");
    std::shared_ptr<Crypter> crypter = std::shared_ptr<Crypter>(new SimpleXORCrypter(key));
    std::shared_ptr<TcpClient> client(new TcpClient(host, port, true, true, true, true, crypter));
    client->registerMessage(1, std::shared_ptr<google::protobuf::Message>(new FooResponse));
    client->registerMessage(2, std::shared_ptr<google::protobuf::Message>(new BanResponse));
    client->registerMessage(3, std::shared_ptr<google::protobuf::Message>(new ServerReady));
    
    if (!client->start()) {
        ERROR_LOG("connect to message server failed\n");
        return nullptr;
    }
    
    // send/recv data to/from client
    bool serverReady = false;
    int16_t rType;
    uint16_t sendTag = 0;
    uint16_t recvTag = 0;
    while (true) {
        if (serverReady) {
//DEBUG_LOG("testRoutine 1\n");
            // send FooRequest
            std::shared_ptr<FooRequest> request(new FooRequest);
            request->set_text("hello world!");
            request->set_times(10);
            
            client->send(1, ++sendTag, true, std::static_pointer_cast<google::protobuf::Message>(request));
        }

        std::shared_ptr<google::protobuf::Message> rMsg;
        do {
//DEBUG_LOG("testRoutine 2\n");
            client->recv(rType, recvTag, rMsg);
            if (!rType) {
                if (!client->isRunning()) {
                    ERROR_LOG("client->recv connection closed\n");

                    // TODO: 断线处理
                    exit(0);
                    return nullptr;
                }

                msleep(1);
            }
        } while (!rType);

        if (serverReady && sendTag != recvTag) {
            ERROR_LOG("tag not match\n");
        }
        
        switch (rType) {
            case 1: {
                std::shared_ptr<FooResponse> response = std::static_pointer_cast<FooResponse>(rMsg);
                //printf("%s\n", response->text().c_str());
                break;
            }
            case 2: {
                std::shared_ptr<BanResponse> response = std::static_pointer_cast<BanResponse>(rMsg);
                //printf("ban %d\n", response->type());
                break;
            }
            case 3: {
                std::shared_ptr<ServerReady> response = std::static_pointer_cast<ServerReady>(rMsg);
                serverReady = true;
                //printf("ban %d\n", response->type());
                break;
            }
            default:
                assert(false);
        }
    }

    return nullptr;
}

void testThread(std::string host, uint16_t port, int num) {
    for (int i = 0; i < num; i++) {
        Address *addr = new Address;
        addr->host = host;
        addr->port = port;
        RoutineEnvironment::startCoroutine(testRoutine, (void*)addr);
    }

    LOG("thread %d running...\n", GetPid());

    RoutineEnvironment::runEventLoop();
}

int main(int argc, const char * argv[])
{
    co_start_hook();

    if(argc<3){
        printf("Usage:\n"
               "echoUdpclt [HOST] [PORT]\n");
        return -1;
    }
    
    std::string host = argv[1];
    uint16_t port = atoi(argv[2]);
    
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigaction( SIGPIPE, &sa, NULL );

    // 启动多个线程创建client
    int threadNum = 1;//4;
    int clientPerThread = 1;//20;
    std::vector<std::thread> threads;
    for (int i = 0; i < threadNum; i++) {
        threads.push_back(std::thread(testThread, host, port, clientPerThread));
    }
    
    RoutineEnvironment::runEventLoop();
    return 0;
}

#endif
