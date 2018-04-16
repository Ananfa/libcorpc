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

using namespace CoRpc;

class ThreadSpecialResource {
public:
    static ThreadSpecialResource& Instance() {
        static ThreadSpecialResource inst;
        return inst;
    }
    
    void setIO(IO *io) { _io = io; }
    
    RpcClient *get_client();
    
private:
    ThreadSpecialResource() {}
    ThreadSpecialResource(ThreadSpecialResource const&);
    ThreadSpecialResource& operator=(ThreadSpecialResource const&);
    ~ThreadSpecialResource() {}
    
private:
    IO *_io;
    
    static __thread RpcClient *_client;
};

__thread RpcClient *ThreadSpecialResource::_client(nullptr);

RpcClient *ThreadSpecialResource::get_client() {
    if (!_client) {
        _client = RpcClient::create(_io);
    }
    
    return _client;
}

class HelloWorldServiceImpl : public HelloWorldService {
public:
    HelloWorldServiceImpl(const std::string& sip, uint32_t sport): _sip(sip), _sport(sport) {}
    virtual void foo(::google::protobuf::RpcController* controller,
                    const ::FooRequest* request,
                    ::FooResponse* response,
                    ::google::protobuf::Closure* done) {
        printf("HelloWorldServiceImpl::foo is called\n");
        
        get_helloworld_clt()->foo(controller, request, response, done);
    }
    
private:
    std::string _sip;
    uint32_t _sport;
    HelloWorldService::Stub *get_helloworld_clt();
    
private:
    static __thread HelloWorldService::Stub *_helloworld_clt;
};

__thread HelloWorldService::Stub *HelloWorldServiceImpl::_helloworld_clt(nullptr);

HelloWorldService::Stub *HelloWorldServiceImpl::get_helloworld_clt() {
    if (!_helloworld_clt) {
        RpcClient *client = ThreadSpecialResource::Instance().get_client();
        if (client) {
            RpcClient::Channel *channel = new RpcClient::Channel(client, _sip, _sport, 1);
            
            _helloworld_clt= new HelloWorldService::Stub(channel, ::google::protobuf::Service::STUB_OWNS_CHANNEL);
        }
    }
    
    return _helloworld_clt;
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    if(argc<5){
        printf("Usage:\n"
               "Tutorial2Middle [IP] [PORT] [ServerIP] [ServerPort] \n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    std::string sip = argv[3];
    unsigned short int sport = atoi(argv[4]);
    
    // 注册服务
    IO *io = IO::create(1, 1);
    
    ThreadSpecialResource::Instance().setIO(io);
    
    RpcServer *server = RpcServer::create(io, 1, ip, port);
    
    HelloWorldServiceImpl *helloWorldService = new HelloWorldServiceImpl(sip, sport);
    server->registerService(helloWorldService);
    
    RoutineEnvironment::runEventLoop();
}

