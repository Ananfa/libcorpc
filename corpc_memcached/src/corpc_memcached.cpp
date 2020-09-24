/*
 * Created by Xianke Liu on 2018/5/14.
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

#include "corpc_memcached.h"

using namespace corpc;

MemcachedConnectPool::Proxy::~Proxy() {
    if (_stub) {
        delete _stub;
    }
}

void MemcachedConnectPool::Proxy::init(corpc::InnerRpcServer *server) {
    InnerRpcChannel *channel = new InnerRpcChannel(server);
    
    _stub = new thirdparty::ThirdPartyService::Stub(channel, thirdparty::ThirdPartyService::STUB_OWNS_CHANNEL);
}

void MemcachedConnectPool::Proxy::callDoneHandle(::google::protobuf::Message *request, corpc::Controller *controller) {
    delete controller;
    delete request;
}

memcached_st* MemcachedConnectPool::Proxy::take() {
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
    
    memcached_st* memc = (memcached_st*)response->handle();
    
    delete controller;
    delete response;
    delete request;
    
    return memc;
}

void MemcachedConnectPool::Proxy::put(memcached_st* memc, bool error) {
    thirdparty::PutRequest *request = new thirdparty::PutRequest();
    Controller *controller = new Controller();
    
    request->set_handle((intptr_t)memc);
    if (error) {
        request->set_error(error);
    }
    
    _stub->put(controller, request, NULL, google::protobuf::NewCallback<::google::protobuf::Message *>(&callDoneHandle, request, controller));
}

MemcachedConnectPool::MemcachedConnectPool(memcached_server_st *memcServers, uint32_t maxConnectNum): _memcServers(memcServers), _maxConnectNum(maxConnectNum), _realConnectCount(0) {
    
}

void MemcachedConnectPool::take(::google::protobuf::RpcController* controller,
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
        memcached_st *memc = memcached_create(NULL);
        
        if (memc) {
            memcached_return rc = memcached_server_push(memc, _memcServers);
            
            if (rc == MEMCACHED_SUCCESS) {
                response->set_handle((intptr_t)memc);
            } else {
                fprintf(stderr, "Couldn't add server: %s\n", memcached_strerror(memc, rc));
                memcached_free(memc);
                memc = NULL;
                
                controller->SetFailed("Couldn't add memcached server");
            }
        } else {
            controller->SetFailed("Couldn't create memcached");
        }
        
        if (!memc) {
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
            controller->SetFailed("can't connect to memcached server");
        } else {
            intptr_t handle = (intptr_t)_idleList.back().handle;
            _idleList.pop_back();
            
            response->set_handle(handle);
        }
    }
}

void MemcachedConnectPool::put(::google::protobuf::RpcController* controller,
                           const thirdparty::PutRequest* request,
                           Void* response,
                           ::google::protobuf::Closure* done) {
    memcached_st *memc = (memcached_st *)request->handle();
    
    if (_idleList.size() < _maxConnectNum) {
        if (request->error()) {
            _realConnectCount--;
            memcached_free(memc);
            
            // 若有等待协程，尝试重连
            if (_waitingList.size() > 0) {
                assert(_idleList.size() == 0);
                
                if (_realConnectCount < _maxConnectNum) {
                    // 注意: 重连后等待列表可能为空（由其他协程释放连接唤醒列表中的等待协程），此时会出bug，因此先从等待列表中取出一个等待协程
                    stCoRoutine_t *co = _waitingList.front();
                    _waitingList.pop_front();
                    
                    _realConnectCount++;
                    memc = memcached_create(NULL);
                    
                    if (memc) {
                        memcached_return rc = memcached_server_push(memc, _memcServers);
                        
                        if (rc == MEMCACHED_SUCCESS) {
                            _idleList.push_back({memc, time(nullptr)});
                        } else {
                            memcached_free(memc);
                            memc = NULL;
                        }
                    }
                    
                    if (!memc) {
                        _realConnectCount--;
                    }
                    
                    // 唤醒先前取出的等待协程
                    co_resume(co);
                    
                    if (!memc) {
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
            _idleList.push_back({memc, time(nullptr)});
            
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
        memcached_free(memc);
    }
}

MemcachedConnectPool* MemcachedConnectPool::create(memcached_server_st *memcServers, uint32_t maxConnectNum) {
    MemcachedConnectPool *pool = new MemcachedConnectPool(memcServers, maxConnectNum);
    pool->init();
    
    return pool;
}

void MemcachedConnectPool::init() {
    _server = InnerRpcServer::create();
    _server->registerService(this);
    proxy.init(_server);
    
    RoutineEnvironment::startCoroutine(clearIdleRoutine, this);
}

void *MemcachedConnectPool::clearIdleRoutine( void *arg ) {
    // 定时清理过期连接
    MemcachedConnectPool *self = (MemcachedConnectPool*)arg;
    
    time_t now = 0;
    
    while (true) {
        sleep(10);
        
        time(&now);
        
        while (self->_idleList.size() > 0 && self->_idleList.front().time < now - 60) {
            DEBUG_LOG("MemcachedConnectPool::clearIdleRoutine -- disconnect a memcached connection");
            memcached_st *handle = self->_idleList.front().handle;
            self->_idleList.pop_front();
            self->_realConnectCount--;
            memcached_free(handle);
        }
    }
    
    return NULL;
}
