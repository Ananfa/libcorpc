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

MysqlConnectPool::Proxy::Proxy(MysqlConnectPool *pool) {
    InnerRpcChannel *channel = new InnerRpcChannel(pool->_server);
    
    _stub = new thirdparty::ThirdPartyService::Stub(channel, thirdparty::ThirdPartyService::STUB_OWNS_CHANNEL);
}

MysqlConnectPool::Proxy::~Proxy() {
    delete _stub;
}

void MysqlConnectPool::Proxy::callDoneHandle(::google::protobuf::Message *request, corpc::Controller *controller) {
    delete controller;
    delete request;
}

MYSQL* MysqlConnectPool::Proxy::take() {
    Void *request = new Void();
    thirdparty::TakeResponse *response = new thirdparty::TakeResponse();
    Controller *controller = new Controller();
    
    _stub->take(controller, request, response, NULL);
    
    if (controller->Failed()) {
        ERROR_LOG("Rpc Call Failed : %s\n", controller->ErrorText().c_str());
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
    
    _stub->put(controller, request, NULL, google::protobuf::NewCallback<::google::protobuf::Message *>(&callDoneHandle, request, controller));
}

MysqlConnectPool::MysqlConnectPool(const char *host, const char *user, const char *passwd, const char *db, unsigned int port, const char *unix_socket, unsigned long clientflag, uint32_t maxConnectNum): _host(host), _user(user), _passwd(passwd), _db(db), _port(port), _unix_socket(unix_socket), _clientflag(clientflag), _maxConnectNum(maxConnectNum), _realConnectCount(0) {
    
}

void MysqlConnectPool::take(::google::protobuf::RpcController* controller,
                            const Void* request,
                            thirdparty::TakeResponse* response,
                            ::google::protobuf::Closure* done) {
    if (_idleList.size() > 0) {
        intptr_t handle = (intptr_t)_idleList.back().handle;
        _idleList.pop_back();
        
        response->set_handle(handle);
    } else if (_realConnectCount < _maxConnectNum) {
        // 建立新连接
        _realConnectCount++;
        MYSQL *con = mysql_init(NULL);
        
        if (con) {
            if (mysql_real_connect(con, _host.c_str(), _user.c_str(), _passwd.c_str(), _db.c_str(), _port, _unix_socket.c_str(), _clientflag)) {
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
            _realConnectCount--;
            
            while (!_waitingList.empty()) {
                stCoRoutine_t *co = _waitingList.front();
                _waitingList.pop_front();
                
                co_resume(co);
            }
        }
        
    } else {
        // 等待空闲连接
        _waitingList.push_back(co_self());
        co_yield_ct();
        
        if (_idleList.size() == 0) {
            controller->SetFailed("can't connect to mysql server");
        } else {
            intptr_t handle = (intptr_t)_idleList.back().handle;
            _idleList.pop_back();
            
            response->set_handle(handle);
        }
    }
}

void MysqlConnectPool::put(::google::protobuf::RpcController* controller,
                           const thirdparty::PutRequest* request,
                           Void* response,
                           ::google::protobuf::Closure* done) {
    MYSQL *con = (MYSQL *)request->handle();
    
    if (_idleList.size() < _maxConnectNum) {
        if (request->error()) {
            _realConnectCount--;
            mysql_close(con);
            
            // 若有等待协程，尝试重连
            if (_waitingList.size() > 0) {
                assert(_idleList.size() == 0);
                
                if (_realConnectCount < _maxConnectNum) {
                    // 注意: 重连后等待列表可能为空（由其他协程释放连接唤醒列表中的等待协程），此时会出bug，因此先从等待列表中取出一个等待协程
                    stCoRoutine_t *co = _waitingList.front();
                    _waitingList.pop_front();
                    
                    _realConnectCount++;
                    MYSQL *con = mysql_init(NULL);
                    
                    if (con) {
                        if (mysql_real_connect(con, _host.c_str(), _user.c_str(), _passwd.c_str(), _db.c_str(), _port, _unix_socket.c_str(), _clientflag)) {
                            _idleList.push_back({con, time(nullptr)});
                        } else {
                            mysql_close(con);
                            con = NULL;
                        }
                    }
                    
                    if (!con) {
                        _realConnectCount--;
                    }
                    
                    // 唤醒先前取出的等待协程
                    co_resume(co);
                    
                    if (!con) {
                        // 唤醒当前所有等待协程
                        while (!_waitingList.empty()) {
                            stCoRoutine_t *co = _waitingList.front();
                            _waitingList.pop_front();
                            
                            co_resume(co);
                        }
                    }
                }
            }
        } else {
            _idleList.push_back({con, time(nullptr)});
            
            if (_waitingList.size() > 0) {
                assert(_idleList.size() == 1);
                stCoRoutine_t *co = _waitingList.front();
                _waitingList.pop_front();
                
                co_resume(co);
            }
        }
    } else {
        assert(_waitingList.size() == 0);
        _realConnectCount--;
        mysql_close(con);
    }
}

MysqlConnectPool* MysqlConnectPool::create(const char *host, const char *user, const char *passwd, const char *db, unsigned int port, const char *unix_socket, unsigned long clientflag, uint32_t maxConnectNum) {
    MysqlConnectPool *pool = new MysqlConnectPool(host, user, passwd, db, port, unix_socket, clientflag, maxConnectNum);
    pool->init();
    
    return pool;
}

void MysqlConnectPool::init() {
    _server = InnerRpcServer::create();
    _server->registerService(this);
    _proxy = new Proxy(this);
    
    RoutineEnvironment::startCoroutine(clearIdleRoutine, this);
}

void *MysqlConnectPool::clearIdleRoutine( void *arg ) {
    // 定时清理过期连接
    MysqlConnectPool *self = (MysqlConnectPool*)arg;
    
    time_t now = 0;
    
    while (true) {
        sleep(10);
        
        time(&now);
        
        while (self->_idleList.size() > 0 && self->_idleList.front().time < now - 60) {
            DEBUG_LOG("MysqlConnectPool::clearIdleRoutine -- disconnect a mysql connection");
            MYSQL *handle = self->_idleList.front().handle;
            self->_idleList.pop_front();
            self->_realConnectCount--;
            mysql_close(handle);
        }
    }
    
    return NULL;
}
