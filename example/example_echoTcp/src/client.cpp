//
//  client1.cpp
//  echoTcp
//
//  Created by Xianke Liu on 2018/5/4.
//Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_message_client.h"
#include "echo.pb.h"

void testThread(std::string host, uint16_t port) {
    std::string key("1234567fvxcvc");
    std::shared_ptr<corpc::Crypter> crypter = std::shared_ptr<corpc::Crypter>(new corpc::SimpleXORCrypter(key));
    corpc::TcpClient client(host, port, true, true, true, true, crypter);
    client.registerMessage(1, std::shared_ptr<google::protobuf::Message>(new FooResponse));
    client.registerMessage(2, std::shared_ptr<google::protobuf::Message>(new BanResponse));
    
    client.start();
    
    // send/recv data to/from client
    uint16_t sendTag = 0;
    uint16_t recvTag = 0;
    while (true) {
        // send FooRequest
        std::shared_ptr<FooRequest> request(new FooRequest);
        request->set_text("hello world!");
        request->set_times(10);
        
        client.send(1, ++sendTag, true, std::static_pointer_cast<google::protobuf::Message>(request));
        
        int16_t rType;
        std::shared_ptr<google::protobuf::Message> rMsg;
        do {
            usleep(100);
            client.recv(rType, recvTag, rMsg);
        } while (!rType);

        if (sendTag != recvTag) {
            printf("Error: tag not match\n");
        }
        
        switch (rType) {
            case 1: {
                std::shared_ptr<FooResponse> response = std::static_pointer_cast<FooResponse>(rMsg);
                //printf("%s\n", response->text().c_str());
                break;
            }
            case 2: {
                std::shared_ptr<BanResponse> response = std::static_pointer_cast<BanResponse>(rMsg);
                //printf("%s\n", response->text().c_str());
                break;
            }
            default:
                assert(false);
        }
    }
}

int main(int argc, const char * argv[])
{
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
    int clientNum = 20;
    std::vector<std::thread> threads;
    for (int i = 0; i < clientNum; i++) {
        threads.push_back(std::thread(testThread, host, port));
    }
    
    for (int i = 0; i < clientNum; i++) {
        threads[i].join();
    }
    
    return 0;
}


