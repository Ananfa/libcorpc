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
#include "corpc_server.h"

#include "helloworld.pb.h"
#include "echo.pb.h"

using namespace CoRpc;

class HelloWorldServiceImpl : public HelloWorldService {
public:
    HelloWorldServiceImpl() {}
    virtual void foo(::google::protobuf::RpcController* controller,
                     const ::FooRequest* request,
                     ::FooResponse* response,
                     ::google::protobuf::Closure* done) {
        printf("HelloWorldServiceImpl::foo is called\n");
        
        std::string msg1 = request->msg1();
        std::string msg2 = request->msg2();
        
        response->set_msg(msg1 + " " + msg2);
    }
};

class EchoServiceImpl : public EchoService {
public:
    EchoServiceImpl() {}
    virtual void echo(::google::protobuf::RpcController* controller,
                     const ::EchoRequest* request,
                     ::EchoResponse* response,
                     ::google::protobuf::Closure* done) {
        printf("EchoServiceImpl::echo is called\n");
        
        std::string msg = request->msg();
        uint32_t seconds = request->seconds();
        
        sleep(seconds);
        
        response->set_msg(msg);
    }
};

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    if(argc<3){
        printf("Usage:\n"
               "Tutorial1Server [IP] [PORT]\n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    
    IO::initialize(1, 1);
    
    Server *server = Server::create(false, 0, ip, port);
    
    HelloWorldServiceImpl *helloWorldService = new HelloWorldServiceImpl();
    EchoServiceImpl *echoService = new EchoServiceImpl();
    server->registerService(helloWorldService);
    server->registerService(echoService);
    
    RoutineEnvironment::runEventLoop();
}