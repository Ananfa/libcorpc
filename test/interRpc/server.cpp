/*
 * Created by Xianke Liu on 2017/11/2.
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
#include "corpc_rpc_server.h"

#include <signal.h>

#include "foo.pb.h"
#include "bar.pb.h"
#include "baz.pb.h"

using namespace corpc;

static int iFooCnt = 0;
static int iBarCnt = 0;
static int iBazCnt = 0;

static void *log_routine( void *arg )
{
    co_enable_hook_sys();
    
    int total = 0;
    int average = 0;
    
    time_t startAt = time(NULL);
    
    while (true) {
        sleep(1);
        
        total += iFooCnt + iBarCnt + iBazCnt;
        
        if (total == 0) {
            startAt = time(NULL);
            continue;
        }
        
        time_t now = time(NULL);
        
        time_t difTime = now - startAt;
        if (difTime > 0) {
            average = total / difTime;
        } else {
            average = total;
        }
        
        printf("time %ld seconds, foo: %d, bar: %d, baz: %d, average: %d, total: %d\n", difTime, iFooCnt, iBarCnt, iBazCnt, average, total);
        
        iFooCnt = 0;
        iBarCnt = 0;
        iBazCnt = 0;
    }
    
    return NULL;
}

class FooServiceImpl : public FooService {
public:
    FooServiceImpl() {}
    virtual void Foo(::google::protobuf::RpcController* controller,
                     const ::FooRequest* request,
                     ::FooResponse* response,
                     ::google::protobuf::Closure* done) {
        std::string str = request->text();
        std::string tmp = str;
        for (int i = 1; i < request->times(); i++)
            str += (" " + tmp);
        response->set_text(str);
        response->set_result(true);
        
        iFooCnt++;
    }
};

class BarServiceImpl : public BarService {
public:
    BarServiceImpl() {}
    virtual void Bar(::google::protobuf::RpcController* controller,
                     const ::BarRequest* request,
                     ::BarResponse* response,
                     ::google::protobuf::Closure* done) {
        std::string str = request->text();
        std::string tmp = str;
        for (int i = 1; i < request->times(); i++)
            str += (" " + tmp);
        response->set_text(str);
        response->set_result(true);
        
        iBarCnt++;
    }
};

class BazServiceImpl : public BazService {
public:
    BazServiceImpl() {}
    virtual void Baz(::google::protobuf::RpcController* controller,
                     const ::BazRequest* request,
                     ::corpc::Void* response,
                     ::google::protobuf::Closure* done) {
        std::string str = request->text();
        
        iBazCnt++;
        //printf("BazServiceImpl::Baz: %s\n", str.c_str());
    }
};

static FooServiceImpl g_fooService;
static BarServiceImpl g_barService;
static BazServiceImpl g_bazService;

int main(int argc, char *argv[]) {
    co_start_hook();
    
    if(argc<3){
        printf("Usage:\n"
               "rpcsvr [IP] [PORT]\n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigaction( SIGPIPE, &sa, NULL );
    
    // 注册服务
    IO *io = IO::create(0, 1);
    
    RpcServer *server = RpcServer::create(io, 0, ip, port);
    server->registerService(&g_fooService);
    server->registerService(&g_barService);
    server->registerService(&g_bazService);
    
    RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    RoutineEnvironment::runEventLoop();
}
