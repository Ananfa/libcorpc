/*
 * Created by Xianke Liu on 2018/3/9.
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
#include "corpc_rpc_client.h"
#include "corpc_rpc_server.h"

#include "helloworld.pb.h"

using namespace corpc;

class HelloWorldServiceImpl : public HelloWorldService {
public:
    HelloWorldServiceImpl(HelloWorldService::Stub *helloworld_clt): _helloworld_clt(helloworld_clt) {}
    virtual void foo(::google::protobuf::RpcController* controller,
                     const ::FooRequest* request,
                     ::FooResponse* response,
                     ::google::protobuf::Closure* done) {
        LOG("HelloWorldServiceImpl::foo is called\n");
        
        _helloworld_clt->foo(controller, request, response, done);
    }
    
private:
    HelloWorldService::Stub *_helloworld_clt;
};

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    if(argc<5){
        LOG("Usage:\n"
               "Tutorial2Middle [IP] [PORT] [HOST] [ServerPort] \n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    std::string host = argv[3];
    unsigned short int sport = atoi(argv[4]);
    
    // 注册服务
    IO *io = IO::create(1, 1);
    
    RpcClient *client = RpcClient::create(io);
    RpcClient::Channel *channel = new RpcClient::Channel(client, host, sport, 1);
    
    //RpcServer *server = RpcServer::create(io, 0, ip, port);
    RpcServer *server = RpcServer::create(io, 1, ip, port);
    
    HelloWorldServiceImpl *helloWorldService = new HelloWorldServiceImpl(new HelloWorldService::Stub(channel));
    server->registerService(helloWorldService);
    
    RoutineEnvironment::runEventLoop();
}
