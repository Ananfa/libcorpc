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
#include "corpc_rpc_client.h"
#include "corpc_rpc_server.h"

#include "factorial.pb.h"

using namespace corpc;

class FactorialServiceImpl : public FactorialService {
public:
    FactorialServiceImpl(IO *io, const std::string& sip, uint32_t sport): _io(io), _sip(sip), _sport(sport), _factorial_clt(NULL) {}
    virtual void factorial(::google::protobuf::RpcController* controller,
                           const ::FactorialRequest* request,
                           ::FactorialResponse* response,
                           ::google::protobuf::Closure* done) {
        uint32_t n = request->n();
        LOG("FactorialServiceImpl::factorial is called for n: %d\n", n);
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
        
        LOG("FactorialServiceImpl::factorial result for %d is %llu\n", n, response->result());
    }
    
private:
    FactorialService::Stub *get_factorial_clt();
    
private:
    IO *_io;
    std::string _sip;
    uint32_t _sport;
    FactorialService::Stub *_factorial_clt;
};

FactorialService::Stub *FactorialServiceImpl::get_factorial_clt() {
    if (!_factorial_clt) {
        RpcClient *client = RpcClient::create(_io);

        RpcClient::Channel *channel = new RpcClient::Channel(client, _sip, _sport, 1);
            
        _factorial_clt= new FactorialService::Stub(channel, ::google::protobuf::Service::STUB_OWNS_CHANNEL);

    }
    
    return _factorial_clt;
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    if(argc<5){
        LOG("Usage:\n"
               "Tutorial2Middle [IP] [PORT] [ServerIP] [ServerPort] \n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    std::string sip = argv[3];
    unsigned short int sport = atoi(argv[4]);
    
    // 注册服务
    IO *io = IO::create(1, 1);
    
    //ThreadSpecialResource::Instance().setIO(io);
    
    RpcServer *server = RpcServer::create(io, 1, ip, port);
    
    FactorialServiceImpl *factorialService = new FactorialServiceImpl(io, sip, sport);
    server->registerService(factorialService);
    
    RoutineEnvironment::runEventLoop();
}
