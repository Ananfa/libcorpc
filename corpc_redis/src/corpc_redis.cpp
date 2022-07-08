/*
 * Created by Xianke Liu on 2018/5/16.
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

#include "corpc_redis.h"

using namespace corpc;

RedisConnectPool::Proxy::~Proxy() {
    if (_stub) {
        delete _stub;
    }
}

void RedisConnectPool::Proxy::init(corpc::InnerRpcServer *server) {
    InnerRpcChannel *channel = new InnerRpcChannel(server);
    
    _stub = new thirdparty::ThirdPartyService::Stub(channel, thirdparty::ThirdPartyService::STUB_OWNS_CHANNEL);
}

redisContext* RedisConnectPool::Proxy::take() {
    Void *request = new Void();
    thirdparty::TakeResponse *response = new thirdparty::TakeResponse();
    Controller *controller = new Controller();
    
    _stub->take(controller, request, response, NULL);
    
    if (controller->Failed()) {
        ERROR_LOG("Rpc Call Failed : %s\n", controller->ErrorText().c_str());
        
        delete controller;
        delete response;
        delete request;
        
        return NULL;
    }
    
    redisContext* redis = (redisContext*)response->handle();
    
    delete controller;
    delete response;
    delete request;
    
    return redis;
}

void RedisConnectPool::Proxy::put(redisContext* redis, bool error) {
    thirdparty::PutRequest *request = new thirdparty::PutRequest();
    Controller *controller = new Controller();
    
    request->set_handle((intptr_t)redis);
    if (error) {
        request->set_error(error);
    }
    
    _stub->put(controller, request, NULL, google::protobuf::NewCallback<::google::protobuf::Message *>(callDoneHandle, request, controller));
}

RedisConnectPool::RedisConnectPool(const char *host, const char *pwd, uint16_t port, uint16_t dbIndex, uint32_t maxConnectNum): _host(host), _passwd(pwd), _port(port), _dbIndex(dbIndex), _maxConnectNum(maxConnectNum), _realConnectCount(0) {
    
}

void RedisConnectPool::take(::google::protobuf::RpcController* controller,
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
        
        struct timeval timeout = { 3, 0 }; // 3 seconds
        redisContext *redis = redisConnectWithTimeout(_host.c_str(), _port, timeout);
        
        if (redis && !redis->err) {
            // 身份认证
            if (!_passwd.empty()) {
                redisReply *reply = (redisReply *)redisCommand(redis,"AUTH %s", _passwd.c_str());
                if (reply == NULL) {
                    controller->SetFailed("auth failed");

                    redisFree(redis);
                    redis = NULL;
                } else {
                    freeReplyObject(reply);
                }
            }

            if (redis) {
                // 选择分库
                if (_dbIndex) {
                    redisReply *reply = (redisReply *)redisCommand(redis,"SELECT %d", _dbIndex);
                    if (reply == NULL) {
                        controller->SetFailed("can't select index");

                        redisFree(redis);
                        redis = NULL;
                    } else {
                        freeReplyObject(reply);
                        response->set_handle((intptr_t)redis);
                    }
                } else {
                    response->set_handle((intptr_t)redis);
                }
            }
        } else if (redis) {
            std::string reason = "can't connect to redis server for ";
            reason.append(redis->errstr);
            controller->SetFailed(reason);
            
            redisFree(redis);
            redis = NULL;
        } else {
            controller->SetFailed("can't allocate redis context");
        }
        
        if (!redis) {
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
            controller->SetFailed("can't connect to redis server");
        } else {
            intptr_t handle = (intptr_t)_idleList.back().handle;
            _idleList.pop_back();
            
            response->set_handle(handle);
        }
    }
}

void RedisConnectPool::put(::google::protobuf::RpcController* controller,
                             const thirdparty::PutRequest* request,
                             Void* response,
                             ::google::protobuf::Closure* done) {
    redisContext *redis = (redisContext *)request->handle();
    
    if (_idleList.size() < _maxConnectNum) {
        if (request->error()) {
            _realConnectCount--;
            redisFree(redis);
            
            // 若有等待协程，尝试重连
            if (_waitingList.size() > 0) {
                assert(_idleList.size() == 0);
                
                if (_realConnectCount < _maxConnectNum) {
                    // 注意: 重连后等待列表可能为空（由其他协程释放连接唤醒列表中的等待协程），此时会出bug，因此先从等待列表中取出一个等待协程
                    stCoRoutine_t *co = _waitingList.front();
                    _waitingList.pop_front();
                    
                    _realConnectCount++;
                    struct timeval timeout = { 1, 500000 }; // 1.5 seconds
                    redisContext *redis = redisConnectWithTimeout(_host.c_str(), _port, timeout);
                    
                    if (redis && !redis->err) {
                        _idleList.push_back({redis, time(nullptr)});
                    } else if (redis) {
                        redisFree(redis);
                        redis = NULL;
                    }
                    
                    if (!redis) {
                        _realConnectCount--;
                    }
                    
                    // 先唤醒先前取出的等待协程
                    co_resume(co);
                    
                    if (!redis) {
                        // 唤醒当前其他所有等待协程
                        while (!_waitingList.empty()) {
                            co = _waitingList.front();
                            _waitingList.pop_front();
                            
                            co_resume(co);
                        }
                    }
                }
            }
        } else {
            _idleList.push_back({redis, time(nullptr)});
            
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
        redisFree(redis);
    }
}

RedisConnectPool* RedisConnectPool::create(const char *host, const char *pwd, uint16_t port, uint16_t dbIndex, uint32_t maxConnectNum) {
    RedisConnectPool *pool = new RedisConnectPool(host, pwd, port, dbIndex, maxConnectNum);
    pool->init();
    
    return pool;
}

void RedisConnectPool::init() {
    _server = new InnerRpcServer();
    _server->registerService(this);
    _server->start();
    proxy.init(_server);
    
    RoutineEnvironment::startCoroutine(clearIdleRoutine, this);
}

void *RedisConnectPool::clearIdleRoutine( void *arg ) {
    // 定时清理过期连接
    RedisConnectPool *self = (RedisConnectPool*)arg;
    
    time_t now = 0;
    
    while (true) {
        sleep(10);
        
        time(&now);
        
        while (self->_idleList.size() > 0 && self->_idleList.front().time < now - 60) {
            DEBUG_LOG("RedisConnectPool::clearIdleRoutine -- disconnect a redis connection\n");
            redisContext *handle = self->_idleList.front().handle;
            self->_idleList.pop_front();
            self->_realConnectCount--;
            redisFree(handle);
        }
    }
    
    return NULL;
}
