/*
 * Created by Xianke Liu on 2018/3/13.
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
#include "corpc_client.h"
#include "corpc_server.h"

#include "factorial.pb.h"

using namespace CoRpc;

class ThreadSpecialResource {
public:
    static ThreadSpecialResource& Instance() {
        static ThreadSpecialResource inst;
        return inst;
    }
    
    void setIO(IO *io) { _io = io; }
    
    Client *get_client();
    
private:
    ThreadSpecialResource() {}
    ThreadSpecialResource(ThreadSpecialResource const&);
    ThreadSpecialResource& operator=(ThreadSpecialResource const&);
    ~ThreadSpecialResource() {}
    
private:
    IO *_io;
    
    static __thread Client *_client;
};

__thread Client *ThreadSpecialResource::_client(nullptr);

Client *ThreadSpecialResource::get_client() {
    if (!_client) {
        _client = Client::create(_io);
    }
    
    return _client;
}

class FactorialServiceImpl : public FactorialService {
public:
    FactorialServiceImpl(const std::string& sip, uint32_t sport): _sip(sip), _sport(sport) {}
    virtual void factorial(::google::protobuf::RpcController* controller,
                     const ::FactorialRequest* request,
                     ::FactorialResponse* response,
                     ::google::protobuf::Closure* done) {
        uint32_t n = request->n();
        printf("FactorialServiceImpl::factorial is called for n: %d\n", n);
        if (n > 20) { // 数太大，无法表示
            response->set_result(0);
        } else if (n > 1) {
            FactorialRequest* req = new FactorialRequest();
            FactorialResponse* resp = new FactorialResponse();
            req->set_n(n - 1);
            get_factorial_clt()->factorial(controller, req, resp, done);
            
            response->set_result(n * resp->result());
        } else {
            response->set_result(1);
        }
        
        printf("FactorialServiceImpl::factorial result for %d is %llu\n", n, response->result());
    }
    
private:
    std::string _sip;
    uint32_t _sport;
    FactorialService::Stub *get_factorial_clt();
    
private:
    static __thread FactorialService::Stub *_factorial_clt;
};

__thread FactorialService::Stub *FactorialServiceImpl::_factorial_clt(nullptr);

FactorialService::Stub *FactorialServiceImpl::get_factorial_clt() {
    if (!_factorial_clt) {
        Client *client = ThreadSpecialResource::Instance().get_client();
        if (client) {
            Client::Channel *channel = new Client::Channel(client, _sip, _sport, 1);
            
            _factorial_clt= new FactorialService::Stub(channel, ::google::protobuf::Service::STUB_OWNS_CHANNEL);
        }
    }
    
    return _factorial_clt;
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
    
    Server *server = Server::create(io, false, 1, ip, port);
    
    FactorialServiceImpl *factorialService = new FactorialServiceImpl(sip, sport);
    server->registerService(factorialService);
    
    RoutineEnvironment::runEventLoop();
}
