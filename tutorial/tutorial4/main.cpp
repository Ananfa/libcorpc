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
#include "corpc_controller.h"
#include "corpc_inner_rpc.h"

#include "helloworld.pb.h"

#include <thread>

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

static void *rpc_routine( void *arg )
{
    co_enable_hook_sys();
    
    InnerRpcClient::Channel *channel = (InnerRpcClient::Channel*)arg;
    
    HelloWorldService::Stub *helloworld_clt = new HelloWorldService::Stub(channel);
    
    FooRequest *request = new FooRequest();
    FooResponse *response = new FooResponse();
    Controller *controller = new Controller();
    
    request->set_msg1("Hello");
    request->set_msg2("World");
    
    helloworld_clt->foo(controller, request, response, NULL);
    
    if (controller->Failed()) {
        printf("Rpc Call Failed : %s\n", controller->ErrorText().c_str());
    } else {
        printf("========= %s =========\n", response->msg().c_str());
    }
    
    delete controller;
    delete response;
    delete request;
    
    delete helloworld_clt;
    
    return NULL;
}

static void clientEntry( InnerRpcServer *server ) {
    InnerRpcClient *client = InnerRpcClient::instance();
    
    InnerRpcClient::Channel *channel = new InnerRpcClient::Channel(client, server);
    
    RoutineEnvironment::startCoroutine(rpc_routine, channel);
    
    RoutineEnvironment::runEventLoop();
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    InnerRpcServer *server = InnerRpcServer::create();
    HelloWorldServiceImpl *helloWorldService = new HelloWorldServiceImpl();
    server->registerService(helloWorldService);
    
    std::thread t = std::thread(clientEntry, server);
    
    RoutineEnvironment::runEventLoop();
    
    return 0;
}
