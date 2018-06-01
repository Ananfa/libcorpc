/*
 * Created by Xianke Liu on 2018/5/15.
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

#include "corpc_mongodb.h"

using namespace corpc;

std::mutex MongodbConnectPool::_initMutex;
bool MongodbConnectPool::_initialized = false;

MongodbConnectPool::Proxy::Proxy(MongodbConnectPool *pool) {
    InnerRpcClient *client = InnerRpcClient::instance();
    
    InnerRpcClient::Channel *channel = new InnerRpcClient::Channel(client, pool->_server);
    
    _stub = new thirdparty::ThirdPartyService::Stub(channel, thirdparty::ThirdPartyService::STUB_OWNS_CHANNEL);
}

MongodbConnectPool::Proxy::~Proxy() {
    delete _stub;
}

void MongodbConnectPool::Proxy::callDoneHandle(::google::protobuf::Message *request, corpc::Controller *controller) {
    delete controller;
    delete request;
}

mongoc_client_t* MongodbConnectPool::Proxy::take() {
    Void *request = new Void();
    thirdparty::TakeResponse *response = new thirdparty::TakeResponse();
    Controller *controller = new Controller();
    
    _stub->take(controller, request, response, NULL);
    
    if (controller->Failed()) {
        fprintf(stderr, "Rpc Call Failed : %s\n", controller->ErrorText().c_str());
        return NULL;
    }
    
    mongoc_client_t* mongoc = (mongoc_client_t*)response->handle();
    
    delete controller;
    delete response;
    delete request;
    
    return mongoc;
}

void MongodbConnectPool::Proxy::put(mongoc_client_t* mongoc, bool error) {
    thirdparty::PutRequest *request = new thirdparty::PutRequest();
    Controller *controller = new Controller();
    
    request->set_handle((intptr_t)mongoc);
    if (error) {
        request->set_error(error);
    }
    
    _stub->put(controller, request, NULL, google::protobuf::NewCallback<::google::protobuf::Message *>(&callDoneHandle, request, controller));
}

MongodbConnectPool::MongodbConnectPool(const char *uri, uint32_t maxConnectNum, uint32_t maxIdleNum): _uri(uri), _maxConnectNum(maxConnectNum), _maxIdleNum(maxIdleNum), _realConnectCount(0) {
    
}

void MongodbConnectPool::take(::google::protobuf::RpcController* controller,
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
        mongoc_client_t *mongoc = mongoc_client_new(_uri.c_str());
        
        if (mongoc) {
            response->set_handle((intptr_t)mongoc);
        } else {
            controller->SetFailed("new mongoc client fail");
            
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
            controller->SetFailed("can't connect to mongodb server");
        } else {
            intptr_t handle = (intptr_t)_idleList.front();
            _idleList.pop_front();
            
            response->set_handle(handle);
        }
    }
}

void MongodbConnectPool::put(::google::protobuf::RpcController* controller,
                               const thirdparty::PutRequest* request,
                               Void* response,
                               ::google::protobuf::Closure* done) {
    mongoc_client_t *mongoc = (mongoc_client_t *)request->handle();
    
    if (_idleList.size() < _maxIdleNum) {
        if (request->error()) {
            _realConnectCount--;
            mongoc_client_destroy(mongoc);
            
            // 若有等待协程，尝试重连
            if (_waitingList.size() > 0) {
                assert(_idleList.size() == 0);
                
                if (_realConnectCount < _maxConnectNum) {
                    // 注意: 重连后等待列表可能为空（由其他协程释放连接唤醒列表中的等待协程），此时会出bug，因此先从等待列表中取出一个等待协程
                    stCoRoutine_t *co = _waitingList.front();
                    _waitingList.pop_front();
                    
                    _realConnectCount++;
                    mongoc = mongoc_client_new(_uri.c_str());
                    
                    if (mongoc) {
                        _idleList.push_back(mongoc);
                    } else {
                        _realConnectCount--;
                    }
                    
                    // 唤醒先前取出的等待协程
                    co_resume(co);
                    
                    if (!mongoc) {
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
            _idleList.push_back(mongoc);
            
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
        mongoc_client_destroy(mongoc);
    }
}

MongodbConnectPool* MongodbConnectPool::create(const char *uri, uint32_t maxConnectNum, uint32_t maxIdleNum) {
    if (!_initialized) {
        std::unique_lock<std::mutex> lock( _initMutex );
        
        if (!_initialized) {
            mongoc_init();
            _initialized = true;
        }
    }
    
    MongodbConnectPool *pool = new MongodbConnectPool(uri, maxConnectNum, maxIdleNum);
    pool->init();
    
    return pool;
}

void MongodbConnectPool::init() {
    _server = InnerRpcServer::create();
    _server->registerService(this);
}

MongodbConnectPool::Proxy* MongodbConnectPool::getProxy() {
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
