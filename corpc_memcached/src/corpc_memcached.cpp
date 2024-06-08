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
    if (stub_) {
        delete stub_;
    }
}

void MemcachedConnectPool::Proxy::init(corpc::InnerRpcServer *server) {
    InnerRpcChannel *channel = new InnerRpcChannel(server);
    
    stub_ = new thirdparty::ThirdPartyService::Stub(channel, thirdparty::ThirdPartyService::STUB_OWNS_CHANNEL);
}

memcached_st* MemcachedConnectPool::Proxy::take() {
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
    
    stub_->put(controller, request, NULL, google::protobuf::NewCallback<::google::protobuf::Message *>(callDoneHandle, request, controller));
}

MemcachedConnectPool::MemcachedConnectPool(memcached_server_st *memcServers, uint32_t maxConnectNum): memcServers_(memcServers), maxConnectNum_(maxConnectNum), realConnectCount_(0) {
    
}

void MemcachedConnectPool::take(::google::protobuf::RpcController* controller,
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
        memcached_st *memc = memcached_create(NULL);
        
        if (memc) {
            memcached_return rc = memcached_server_push(memc, memcServers_);
            
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
            controller->SetFailed("can't connect to memcached server");
        } else {
            intptr_t handle = (intptr_t)idleList_.back().handle;
            idleList_.pop_back();
            
            response->set_handle(handle);
        }
    }
}

void MemcachedConnectPool::put(::google::protobuf::RpcController* controller,
                           const thirdparty::PutRequest* request,
                           Void* response,
                           ::google::protobuf::Closure* done) {
    memcached_st *memc = (memcached_st *)request->handle();
    
    if (idleList_.size() < maxConnectNum_) {
        if (request->error()) {
            realConnectCount_--;
            memcached_free(memc);
            
            // 若有等待协程，尝试重连
            if (waitingList_.size() > 0) {
                assert(idleList_.size() == 0);
                
                if (realConnectCount_ < maxConnectNum_) {
                    // 注意: 重连后等待列表可能为空（由其他协程释放连接唤醒列表中的等待协程），此时会出bug，因此先从等待列表中取出一个等待协程
                    stCoRoutine_t *co = waitingList_.front();
                    waitingList_.pop_front();
                    
                    realConnectCount_++;
                    memc = memcached_create(NULL);
                    
                    if (memc) {
                        memcached_return rc = memcached_server_push(memc, memcServers_);
                        
                        if (rc == MEMCACHED_SUCCESS) {
                            idleList_.push_back({memc, time(nullptr)});
                        } else {
                            memcached_free(memc);
                            memc = NULL;
                        }
                    }
                    
                    if (!memc) {
                        realConnectCount_--;
                    }
                    
                    // 唤醒先前取出的等待协程
                    co_resume(co);
                    
                    if (!memc) {
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
            idleList_.push_back({memc, time(nullptr)});
            
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
        memcached_free(memc);
    }
}

MemcachedConnectPool* MemcachedConnectPool::create(memcached_server_st *memcServers, uint32_t maxConnectNum) {
    MemcachedConnectPool *pool = new MemcachedConnectPool(memcServers, maxConnectNum);
    pool->init();
    
    return pool;
}

void MemcachedConnectPool::init() {
    server_ = new InnerRpcServer();
    server_->registerService(this);
    server_->start();
    proxy.init(server_);
    
    RoutineEnvironment::startCoroutine(clearIdleRoutine, this);
}

void *MemcachedConnectPool::clearIdleRoutine( void *arg ) {
    // 定时清理过期连接
    MemcachedConnectPool *self = (MemcachedConnectPool*)arg;
    
    time_t now = 0;
    
    while (true) {
        sleep(10);
        
        time(&now);
        
        while (self->idleList_.size() > 0 && self->idleList_.front().time < now - 60) {
            DEBUG_LOG("MemcachedConnectPool::clearIdleRoutine -- disconnect a memcached connection");
            memcached_st *handle = self->idleList_.front().handle;
            self->idleList_.pop_front();
            self->realConnectCount_--;
            memcached_free(handle);
        }
    }
    
    return NULL;
}
