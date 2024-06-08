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
    if (stub_) {
        delete stub_;
    }
}

void RedisConnectPool::Proxy::init(corpc::InnerRpcServer *server) {
    InnerRpcChannel *channel = new InnerRpcChannel(server);
    
    stub_ = new thirdparty::ThirdPartyService::Stub(channel, thirdparty::ThirdPartyService::STUB_OWNS_CHANNEL);
}

redisContext* RedisConnectPool::Proxy::take() {
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
    
    stub_->put(controller, request, NULL, google::protobuf::NewCallback<::google::protobuf::Message *>(callDoneHandle, request, controller));
}

RedisConnectPool::RedisConnectPool(const char *host, const char *pwd, uint16_t port, uint16_t dbIndex, uint32_t maxConnectNum): host_(host), passwd_(pwd), port_(port), dbIndex_(dbIndex), maxConnectNum_(maxConnectNum), realConnectCount_(0) {
    
}

void RedisConnectPool::take(::google::protobuf::RpcController* controller,
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
        
        struct timeval timeout = { 3, 0 }; // 3 seconds
        redisContext *redis = redisConnectWithTimeout(host_.c_str(), port_, timeout);
        
        if (redis && !redis->err) {
            // 身份认证
            if (!passwd_.empty()) {
                redisReply *reply = (redisReply *)redisCommand(redis,"AUTH %s", passwd_.c_str());
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
                if (dbIndex_) {
                    redisReply *reply = (redisReply *)redisCommand(redis,"SELECT %d", dbIndex_);
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
            controller->SetFailed("can't connect to redis server");
        } else {
            intptr_t handle = (intptr_t)idleList_.back().handle;
            idleList_.pop_back();
            
            response->set_handle(handle);
        }
    }
}

void RedisConnectPool::put(::google::protobuf::RpcController* controller,
                             const thirdparty::PutRequest* request,
                             Void* response,
                             ::google::protobuf::Closure* done) {
    redisContext *redis = (redisContext *)request->handle();
    
    if (idleList_.size() < maxConnectNum_) {
        if (request->error()) {
            realConnectCount_--;
            redisFree(redis);
            
            // 若有等待协程，尝试重连
            if (waitingList_.size() > 0) {
                assert(idleList_.size() == 0);
                
                if (realConnectCount_ < maxConnectNum_) {
                    // 注意: 重连后等待列表可能为空（由其他协程释放连接唤醒列表中的等待协程），此时会出bug，因此先从等待列表中取出一个等待协程
                    stCoRoutine_t *co = waitingList_.front();
                    waitingList_.pop_front();
                    
                    realConnectCount_++;
                    struct timeval timeout = { 1, 500000 }; // 1.5 seconds
                    redisContext *redis = redisConnectWithTimeout(host_.c_str(), port_, timeout);
                    
                    if (redis && !redis->err) {
                        idleList_.push_back({redis, time(nullptr)});
                    } else if (redis) {
                        redisFree(redis);
                        redis = NULL;
                    }
                    
                    if (!redis) {
                        realConnectCount_--;
                    }
                    
                    // 先唤醒先前取出的等待协程
                    co_resume(co);
                    
                    if (!redis) {
                        // 唤醒当前其他所有等待协程
                        while (!waitingList_.empty()) {
                            co = waitingList_.front();
                            waitingList_.pop_front();
                            
                            co_resume(co);
                        }
                    }
                }
            }
        } else {
            idleList_.push_back({redis, time(nullptr)});
            
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
        redisFree(redis);
    }
}

RedisConnectPool* RedisConnectPool::create(const char *host, const char *pwd, uint16_t port, uint16_t dbIndex, uint32_t maxConnectNum) {
    RedisConnectPool *pool = new RedisConnectPool(host, pwd, port, dbIndex, maxConnectNum);
    pool->init();
    
    return pool;
}

void RedisConnectPool::init() {
    server_ = new InnerRpcServer();
    server_->registerService(this);
    server_->start();
    proxy.init(server_);
    
    RoutineEnvironment::startCoroutine(clearIdleRoutine, this);
}

void *RedisConnectPool::clearIdleRoutine( void *arg ) {
    // 定时清理过期连接
    RedisConnectPool *self = (RedisConnectPool*)arg;
    
    time_t now = 0;
    
    while (true) {
        sleep(10);
        
        time(&now);
        
        while (self->idleList_.size() > 0 && self->idleList_.front().time < now - 60) {
            DEBUG_LOG("RedisConnectPool::clearIdleRoutine -- disconnect a redis connection\n");
            redisContext *handle = self->idleList_.front().handle;
            self->idleList_.pop_front();
            self->realConnectCount_--;
            redisFree(handle);
        }
    }
    
    return NULL;
}
