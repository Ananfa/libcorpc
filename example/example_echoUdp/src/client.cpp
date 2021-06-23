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

#include "corpc_message_client.h"
#include "echo.pb.h"

#define LOCAL_PORT 20020

void testThread(std::string host, uint16_t port, uint16_t local_port) {
    std::string key("1234567fvxcvc");
    std::shared_ptr<corpc::Crypter> crypter = std::shared_ptr<corpc::Crypter>(new corpc::SimpleXORCrypter(key));
    corpc::UdpClient client(host, port, local_port, true, true, true, false, crypter);
    client.registerMessage(1, new FooResponse);
    client.registerMessage(2, new BanResponse);
    
    client.start();
    
    // 注意：这里等待udp连接真正建立，不然后续发送消息会与握手过程混淆
    // TODO：判断握手成功之后才做后续逻辑
    //sleep(5);

    // send/recv data to/from client
    while (true) {
        // send FooRequest
        FooRequest *request = new FooRequest;
        request->set_text("hello world!");
        request->set_times(2);
        
        client.send(1, 0, request);
        
        int16_t rType;
        uint16_t tag;
        google::protobuf::Message *rMsg;
        do {
            usleep(100);
            client.recv(rType, tag, rMsg);
        } while (!rType);
        
        switch (rType) {
            case 1: {
                FooResponse *response = (FooResponse*)rMsg;
                //printf("%s\n", response->text().c_str());
                delete response;
                break;
            }
            case 2: {
                BanResponse *response = (BanResponse*)rMsg;
                //printf("%s\n", response->text().c_str());
                delete response;
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
        threads.push_back(std::thread(testThread, host, port, LOCAL_PORT+i));
    }
    
    for (int i = 0; i < clientNum; i++) {
        threads[i].join();
    }
    
    return 0;
}

