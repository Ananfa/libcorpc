/*
 * Created by Xianke Liu on 2018/4/28.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "corpc_routine_env.h"
#include "corpc_message_client.h"
#include "echo.pb.h"
#include <signal.h>
#include <thread>

using namespace corpc;

#define LOCAL_PORT 20020

struct Address {
    std::string host;
    uint16_t port;
    uint16_t local_port;
};

void *testRoutine(void *arg) {
    Address *addr = (Address *)arg;
    std::string host = addr->host;
    uint16_t port = addr->port;
    uint16_t local_port = addr->local_port;
    delete addr;

    std::string key("1234567fvxcvc");
    std::shared_ptr<Crypter> crypter = std::shared_ptr<Crypter>(new SimpleXORCrypter(key));
    std::shared_ptr<UdpClient> client(new UdpClient(host, port, local_port, true, true, true, false, crypter));
    client->registerMessage(1, std::shared_ptr<google::protobuf::Message>(new FooResponse));
    client->registerMessage(2, std::shared_ptr<google::protobuf::Message>(new BanResponse));
    client->registerMessage(3, std::shared_ptr<google::protobuf::Message>(new ServerReady));
    
    if (!client->start()) {
        ERROR_LOG("connect to message server failed\n");
        return nullptr;
    }
    
    // 注意：这里等待udp连接真正建立，不然后续发送消息会与握手过程混淆
    // TODO：判断握手成功之后才做后续逻辑
    // 目前udp client的实现中加了个开关，等待收到服务器handshake4消息才打开开关（允许发送消息）
    //sleep(5);

    // send/recv data to/from client
    bool serverReady = false;
    int16_t rType;
    uint16_t tag;
    while (true) {
        if (serverReady) {
            // send FooRequest
            std::shared_ptr<FooRequest> request(new FooRequest);
            request->set_text("hello world!");
            request->set_times(2);
            
            client->send(1, 0, true, std::static_pointer_cast<google::protobuf::Message>(request));
        }
        
        std::shared_ptr<google::protobuf::Message> rMsg;
        do {
            client->recv(rType, tag, rMsg);
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

void testThread(std::string host, uint16_t port, uint16_t local_port, int num) {
    for (int i = 0; i < num; i++) {
        Address *addr = new Address;
        addr->host = host;
        addr->port = port;
        addr->local_port = local_port + i;
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
    int threadNum = 4;
    int clientPerThread = 40;
    std::vector<std::thread> threads;
    for (int i = 0; i < threadNum; i++) {
        uint16_t local_port = LOCAL_PORT + i * clientPerThread;
        threads.push_back(std::thread(testThread, host, port, local_port, clientPerThread));
    }
    
    RoutineEnvironment::runEventLoop();
    return 0;
}

