/*
 * Created by Xianke Liu on 2018/5/8.
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

#include "corpc_mysql.h"
#include <time.h>

using namespace corpc;

MysqlConnectPool::Proxy::~Proxy() {
    if (stub_) {
        delete stub_;
    }
}

void MysqlConnectPool::Proxy::init(corpc::InnerRpcServer *server) {
    InnerRpcChannel *channel = new InnerRpcChannel(server);
    
    stub_ = new thirdparty::ThirdPartyService::Stub(channel, thirdparty::ThirdPartyService::STUB_OWNS_CHANNEL);
}
    
MYSQL* MysqlConnectPool::Proxy::take() {
    Void *request = new Void();
    thirdparty::TakeResponse *response = new thirdparty::TakeResponse();
    Controller *controller = new Controller();
    
    stub_->take(controller, request, response, NULL);
    
    if (controller->Failed()) {
        ERROR_LOG("Rpc Call Failed : %s\n", controller->ErrorText().c_str());
        
        delete controller;
        delete response;
        delete request;
        
        return NULL;
    }
    
    MYSQL* mysql = (MYSQL*)response->handle();
    
    delete controller;
    delete response;
    delete request;
    
    return mysql;
}

void MysqlConnectPool::Proxy::put(MYSQL* mysql, bool error) {
    thirdparty::PutRequest *request = new thirdparty::PutRequest();
    Controller *controller = new Controller();
    
    request->set_handle((intptr_t)mysql);
    if (error) {
        request->set_error(error);
    }
    
    stub_->put(controller, request, NULL, google::protobuf::NewCallback<::google::protobuf::Message *>(callDoneHandle, request, controller));
}

MysqlConnectPool::MysqlConnectPool(const char *host, const char *user, const char *passwd, const char *db, unsigned int port, const char *unix_socket, unsigned long clientflag, uint32_t maxConnectNum): host_(host), user_(user), passwd_(passwd), db_(db), port_(port), unix_socket_(unix_socket), clientflag_(clientflag), maxConnectNum_(maxConnectNum), realConnectCount_(0) {
    
}

void MysqlConnectPool::take(::google::protobuf::RpcController* controller,
                            const Void* request,
                            thirdparty::TakeResponse* response,
                            ::google::protobuf::Closure* done) {
    if (idleList_.size() > 0) {
        intptr_t handle = (intptr_t)idleList_.back().handle;
        idleList_.pop_back();
        
        response->set_handle(handle);
    } else if (realConnectCount_ < maxConnectNum_) {
        // 建立新连接
        realConnectCount_++;
        MYSQL *con = mysql_init(NULL);
        
        if (con) {
            if (mysql_real_connect(con, host_.c_str(), user_.c_str(), passwd_.c_str(), db_.c_str(), port_, unix_socket_.c_str(), clientflag_)) {
                response->set_handle((intptr_t)con);
            } else {
                mysql_close(con);
                con = NULL;
                
                controller->SetFailed("can't connect to mysql server");
            }
        } else {
            controller->SetFailed("can't create MYSQL object probably because not enough memory");
        }
        
        if (!con) {
            // 唤醒所有等待协程
            realConnectCount_--;
            
            while (!waitingList_.empty()) {
                stCoRoutine_t *co = waitingList_.front();
                waitingList_.pop_front();
                
                co_resume(co);
            }
        }
        
    } else {
        // 等待空闲连接
        waitingList_.push_back(co_self());
        co_yield_ct();
        
        if (idleList_.size() == 0) {
            controller->SetFailed("can't connect to mysql server");
        } else {
            intptr_t handle = (intptr_t)idleList_.back().handle;
            idleList_.pop_back();
            
            response->set_handle(handle);
        }
    }
}

void MysqlConnectPool::put(::google::protobuf::RpcController* controller,
                           const thirdparty::PutRequest* request,
                           Void* response,
                           ::google::protobuf::Closure* done) {
    MYSQL *con = (MYSQL *)request->handle();
    
    if (idleList_.size() < maxConnectNum_) {
        if (request->error()) {
            realConnectCount_--;
            mysql_close(con);
            
            // 若有等待协程，尝试重连
            if (waitingList_.size() > 0) {
                assert(idleList_.size() == 0);
                
                if (realConnectCount_ < maxConnectNum_) {
                    // 注意: 重连后等待列表可能为空（由其他协程释放连接唤醒列表中的等待协程），此时会出bug，因此先从等待列表中取出一个等待协程
                    stCoRoutine_t *co = waitingList_.front();
                    waitingList_.pop_front();
                    
                    realConnectCount_++;
                    MYSQL *con = mysql_init(NULL);
                    
                    if (con) {
                        if (mysql_real_connect(con, host_.c_str(), user_.c_str(), passwd_.c_str(), db_.c_str(), port_, unix_socket_.c_str(), clientflag_)) {
                            idleList_.push_back({con, time(nullptr)});
                        } else {
                            mysql_close(con);
                            con = NULL;
                        }
                    }
                    
                    if (!con) {
                        realConnectCount_--;
                    }
                    
                    // 唤醒先前取出的等待协程
                    co_resume(co);
                    
                    if (!con) {
                        // 唤醒当前所有等待协程
                        while (!waitingList_.empty()) {
                            stCoRoutine_t *co = waitingList_.front();
                            waitingList_.pop_front();
                            
                            co_resume(co);
                        }
                    }
                }
            }
        } else {
            idleList_.push_back({con, time(nullptr)});
            
            if (waitingList_.size() > 0) {
                assert(idleList_.size() == 1);
                stCoRoutine_t *co = waitingList_.front();
                waitingList_.pop_front();
                
                co_resume(co);
            }
        }
    } else {
        assert(waitingList_.size() == 0);
        realConnectCount_--;
        mysql_close(con);
    }
}

MysqlConnectPool* MysqlConnectPool::create(const char *host, const char *user, const char *passwd, const char *db, unsigned int port, const char *unix_socket, unsigned long clientflag, uint32_t maxConnectNum) {
    MysqlConnectPool *pool = new MysqlConnectPool(host, user, passwd, db, port, unix_socket, clientflag, maxConnectNum);
    pool->init();
    
    return pool;
}

void MysqlConnectPool::init() {
    server_ = new InnerRpcServer();
    server_->registerService(this);
    server_->start();
    proxy.init(server_);
    
    RoutineEnvironment::startCoroutine(clearIdleRoutine, this);
}

void *MysqlConnectPool::clearIdleRoutine( void *arg ) {
    // 定时清理过期连接
    MysqlConnectPool *self = (MysqlConnectPool*)arg;
    
    time_t now = 0;
    
    while (true) {
        sleep(10);
        
        time(&now);
        
        while (self->idleList_.size() > 0 && self->idleList_.front().time < now - 60) {
            DEBUG_LOG("MysqlConnectPool::clearIdleRoutine -- disconnect a mysql connection");
            MYSQL *handle = self->idleList_.front().handle;
            self->idleList_.pop_front();
            self->realConnectCount_--;
            mysql_close(handle);
        }
    }
    
    return NULL;
}
