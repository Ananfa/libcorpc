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

RedisConnectPool::Proxy::Proxy(RedisConnectPool *pool) {
    InnerRpcClient *client = InnerRpcClient::instance();
    
    InnerRpcClient::Channel *channel = new InnerRpcClient::Channel(client, pool->_server);
    
    _stub = new thirdparty::ThirdPartyService::Stub(channel, thirdparty::ThirdPartyService::STUB_OWNS_CHANNEL);
}

RedisConnectPool::Proxy::~Proxy() {
    delete _stub;
}

void RedisConnectPool::Proxy::callDoneHandle(::google::protobuf::Message *request, corpc::Controller *controller) {
    delete controller;
    delete request;
}

redisContext* RedisConnectPool::Proxy::take() {
    Void *request = new Void();
    thirdparty::TakeResponse *response = new thirdparty::TakeResponse();
    Controller *controller = new Controller();
    
    _stub->take(controller, request, response, NULL);
    
    if (controller->Failed()) {
        fprintf(stderr, "Rpc Call Failed : %s\n", controller->ErrorText().c_str());
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
    
    _stub->put(controller, request, NULL, google::protobuf::NewCallback<::google::protobuf::Message *>(&callDoneHandle, request, controller));
}

RedisConnectPool::RedisConnectPool(const char *host, unsigned int port, uint32_t maxConnectNum, uint32_t maxIdleNum): _host(host), _port(port), _maxConnectNum(maxConnectNum), _maxIdleNum(maxIdleNum), _realConnectCount(0) {
    
}

void RedisConnectPool::take(::google::protobuf::RpcController* controller,
                              const Void* request,
                              thirdparty::TakeResponse* response,
                              ::google::protobuf::Closure* done) {
    if (_idleList.size() > 0) {
        intptr_t handle = (intptr_t)_idleList.front();
        _idleList.pop_front();
        
        response->set_handle(handle);
    } else if (_realConnectCount < _maxConnectNum) {
        // 建立新连接
        _realConnectCount++;
        
        struct timeval timeout = { 3, 0 }; // 3 seconds
        redisContext *redis = redisConnectWithTimeout(_host.c_str(), _port, timeout);
        
        if (redis && !redis->err) {
            response->set_handle((intptr_t)redis);
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
            intptr_t handle = (intptr_t)_idleList.front();
            _idleList.pop_front();
            
            response->set_handle(handle);
        }
    }
}

void RedisConnectPool::put(::google::protobuf::RpcController* controller,
                             const thirdparty::PutRequest* request,
                             Void* response,
                             ::google::protobuf::Closure* done) {
    redisContext *redis = (redisContext *)request->handle();
    
    if (_idleList.size() < _maxIdleNum) {
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
                        _idleList.push_back(redis);
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
            _idleList.push_back(redis);
            
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

RedisConnectPool* RedisConnectPool::create(const char *host, unsigned int port, uint32_t maxConnectNum, uint32_t maxIdleNum) {
    RedisConnectPool *pool = new RedisConnectPool(host, port, maxConnectNum, maxIdleNum);
    pool->init();
    
    return pool;
}

void RedisConnectPool::init() {
    _server = InnerRpcServer::create();
    _server->registerService(this);
}

RedisConnectPool::Proxy* RedisConnectPool::getProxy() {
    pid_t pid = GetPid();
    
    auto iter = _threadProxyMap.find(pid);
    if (iter != _threadProxyMap.end()) {
        return iter->second;
    }
    
    // 为当前线程创建proxy
    Proxy *proxy = new Proxy(this);
    _threadProxyMap.insert(std::make_pair(pid, proxy));
    
    return proxy;
}
