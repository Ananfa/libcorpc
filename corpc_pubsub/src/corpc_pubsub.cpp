/*
 * Created by Xianke Liu on 2020/9/17.
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

#include "corpc_pubsub.h"
#include "corpc_routine_env.h"
#include <string.h>

using namespace corpc;

PubsubService* PubsubService::service_ = nullptr;

PubsubService::PubsubService(RedisConnectPool *redisPool): redisPool_(redisPool), subCon_(nullptr) {
    pipe(pipe_.pipefd);
    
    co_register_fd(pipe_.pipefd[1]);
    co_set_nonblock(pipe_.pipefd[1]);
}

bool PubsubService::StartPubsubService(RedisConnectPool *redisPool) {
    if (service_ != nullptr) {
        return false;
    }

    service_ = new PubsubService(redisPool);
    service_->_start();

    return true;
}

bool PubsubService::Subscribe(const std::string& topic, bool needCoroutine, SubcribeCallback callback) {
    if (service_ == nullptr) {
        return false;
    }

    return service_->_subscribe(topic, needCoroutine, callback);
}

bool PubsubService::Publish(const std::string& topic, const std::string& msg) {
    if (service_ == nullptr) {
        return false;
    }

    return service_->_publish(topic, msg);
}

void PubsubService::threadEntry(PubsubService *self) {
    RoutineEnvironment::startCoroutine(subscribeRoutine, self);
    RoutineEnvironment::startCoroutine(registerRoutine, self);
    RoutineEnvironment::runEventLoop();
}

void *PubsubService::subscribeRoutine(void *arg) {
    PubsubService *self = (PubsubService *)arg;
    int pReadFd = self->pipe_.pipefd[0];
    co_register_fd(pReadFd);
    co_set_timeout(pReadFd, -1, 1000);

    std::vector<char> buf(16);
    std::string commandStr;

    RedisConnectPool::Proxy& proxy = self->redisPool_->proxy;

    redisContext *redis = nullptr;
    redisReply *reply = nullptr;

    struct pollfd fds[2];
    fds[0].fd = pReadFd;
    fds[0].events = POLLIN;
    while (true) {
        int nfds = 1;
        if (self->topicToEnvsMap_.size() > 0) {
            //DEBUG_LOG("PubsubService::subscribeRoutine -- redis SUBSCRIBE\n");
            commandStr = "SUBSCRIBE";
            for (auto it = self->topicToEnvsMap_.begin(); it != self->topicToEnvsMap_.end(); ++it) {
                commandStr += " ";
                commandStr += it->first;
            }

            if (!redis) {
                redis = proxy.take();
            }

            reply = (redisReply *)redisCommand(redis, commandStr.c_str());

            //DEBUG_LOG("PubsubService::subscribeRoutine -- SUBSCRIBE elements: %d\n", reply->elements);
            //DEBUG_LOG("PubsubService::subscribeRoutine -- SUBSCRIBE reply: %s %s %s-%d\n",reply->element[0]->str,reply->element[1]->str, reply->element[2]->str, reply->element[2]->type);
            
            freeReplyObject(reply);

            fds[1].fd = redis->fd;
            fds[1].events = POLLIN;
            nfds = 2;
        }

        while (true) {
            int ret = poll(fds, nfds, 60000); // 每60秒检测一次
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }

                ERROR_LOG("PubsubService::subscribeRoutine -- poll ret:%d\n", ret);
                exit(0);
            } else if (ret > 0) {
                if (nfds == 2) {
                    // 先处理订阅消息
                    // 增加订阅主题消息应该是在服务器启动时，此时应该没有订阅消息产生
                    if ((fds[1].revents & POLLIN) == POLLIN) {
                        //DEBUG_LOG("PubsubService::subscribeRoutine -- fds[1] pollin\n");
                        if (redisGetReply(redis, (void**)&reply) == REDIS_OK) {
                            // 向订阅主题环境发消息
                            assert(reply->elements == 3);
                            //DEBUG_LOG("PubsubService::subscribeRoutine -- redisGetReply: %s %s %s\n",reply->element[0]->str,reply->element[1]->str, reply->element[2]->str);
                            if (strcmp(reply->element[0]->str, "message") == 0) {
                                auto it = self->topicToEnvsMap_.find(reply->element[1]->str);
                                if (it != self->topicToEnvsMap_.end()) {
                                    std::shared_ptr<TopicMessage> tmsg(new TopicMessage());
                                    tmsg->topic.assign(reply->element[1]->str, reply->element[1]->len);
                                    tmsg->message.assign(reply->element[2]->str, reply->element[2]->len);
                                    for (auto it1 = it->second.begin(); it1 != it->second.end(); ++it1) {
                                        (*it1)->queue_.push(tmsg);
                                    }
                                } else {
                                    ERROR_LOG("PubsubService::subscribeRoutine -- unknown topic: %s\n", reply->element[1]->str);
                                }
                            }
                            
                            freeReplyObject(reply);
                        } else {
                            ERROR_LOG("PubsubService::subscribeRoutine -- redisGetReply failed\n");
                            // redis连接故障，需要重新连接
                            proxy.put(redis, true);
                            redis = nullptr;
                            break;
                        }
                    }
                }

                // 再处理订阅主题变化消息（如果有主题消息增删(注意：目前没有删)，退到上层循环）
                if ((fds[0].revents & POLLIN) == POLLIN) {
                    //DEBUG_LOG("PubsubService::subscribeRoutine -- fds[0] pollin\n");
                    ret = read(pReadFd, &buf[0], 16);
                    //DEBUG_LOG("PubsubService::subscribeRoutine -- fds[0] pollin ret:%d\n", ret);
                    assert(ret > 0);
                    
                    if (redis) {
                        //DEBUG_LOG("PubsubService::subscribeRoutine -- redis UNSUBSCRIBE\n");
                        reply = (redisReply *)redisCommand(redis, "UNSUBSCRIBE");
                        //DEBUG_LOG("PubsubService::subscribeRoutine -- UNSUBSCRIBE elements: %d\n", reply->elements);
                        //DEBUG_LOG("PubsubService::subscribeRoutine -- UNSUBSCRIBE reply: %s %s %s-%d\n",reply->element[0]->str,reply->element[1]->str, reply->element[2]->str, reply->element[2]->type);
                        freeReplyObject(reply);
                    }

                    break;
                }
            }
        }
    }
}

void *PubsubService::registerRoutine(void *arg) {
    PubsubService *self = (PubsubService *)arg;

    TopicRegisterMessageQueue& queue = self->queue_;

    int readFd = queue.getReadFd();
    co_register_fd(readFd);
    co_set_timeout(readFd, -1, 1000);
    int ret;
    std::vector<char> buf(1024);
    while (true) {
        // 等待处理信号
        ret = (int)read(readFd, &buf[0], 1024);
        assert(ret != 0);
        if (ret < 0) {
            if (errno == EAGAIN) {
                continue;
            } else {
                // 管道出错
                ERROR_LOG("PubsubService::registerRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                       readFd, ret, errno, strerror(errno));
                
                // TODO: 如何处理？退出协程？
                // sleep 10 milisecond
                msleep(10);
            }
        }

        // 登记主题订阅环境
        TopicRegisterMessage *msg = queue.pop();
        while (msg) {
            if (self->topicToEnvsMap_.find(msg->topic) == self->topicToEnvsMap_.end()) {
                char buf = 'X';
                write(self->pipe_.pipefd[1], &buf, 1);
            }
            
            self->topicToEnvsMap_[msg->topic].push_back(msg->env);
            delete msg;
            msg = queue.pop();
        }
    }
}

void PubsubService::_start() {
    // 启动处理线程
    t_ = std::thread(threadEntry, this);
}

bool PubsubService::_subscribe(const std::string& topic, bool needCoroutine, SubcribeCallback callback) {
    if (service_ == nullptr) {
        ERROR_LOG("PubsubService::_subscribe -- service not start\n");
        return false;
    }

    // 注册订阅回调
    SubscribeEnv::getEnv()->addSubscribeCallback(topic, needCoroutine, callback);
    return true;
}

bool PubsubService::_publish(const std::string& topic, const std::string& msg) {
    RedisConnectPool::Proxy& proxy = redisPool_->proxy;
    redisContext *redis = proxy.take();
    if (redis == nullptr) {
        return false;
    }

    redisReply *reply = (redisReply *)redisCommand(redis, "PUBLISH %s %b", topic.c_str(), msg.c_str(), msg.size());
    if (reply == nullptr || redis->err) {
        if (reply != nullptr) {
            freeReplyObject(reply);
        }

        proxy.put(redis, true);
    } else {
        proxy.put(redis, false);
    }
}

static __thread SubscribeEnv* g_subscribeEnv = nullptr;

SubscribeEnv *SubscribeEnv::getEnv() {
    if (g_subscribeEnv == nullptr) {
        g_subscribeEnv = new SubscribeEnv();
        g_subscribeEnv->initialize();
    }

    return g_subscribeEnv;
}

void SubscribeEnv::initialize() {
    // 启动回调消息处理协程
    RoutineEnvironment::startCoroutine(deamonRoutine, this);
}

void SubscribeEnv::addSubscribeCallback(const std::string& topic, bool needCoroutine, SubcribeCallback callback) {
    topicCallbackMap_[topic].push_back({needCoroutine, callback});

    // 若第一次订阅主题，将主题和订阅队列传给_service线程的登记协程
    if (topicCallbackMap_[topic].size() == 1) {
        TopicRegisterMessage *msg = new TopicRegisterMessage();
        msg->topic = topic;
        msg->env = this;
        PubsubService::service_->queue_.push(msg);
    }
}

void *SubscribeEnv::deamonRoutine(void *arg) {
    SubscribeEnv *self = (SubscribeEnv *)arg;

    TopicMessageQueue& queue = self->queue_;

    int readFd = queue.getReadFd();
    co_register_fd(readFd);
    co_set_timeout(readFd, -1, 1000);
    int ret;
    std::vector<char> buf(1024);
    while (true) {
        // 等待处理信号
        ret = (int)read(readFd, &buf[0], 1024);
        assert(ret != 0);
        if (ret < 0) {
            if (errno == EAGAIN) {
                continue;
            } else {
                // 管道出错
                ERROR_LOG("SubscribeEnv::deamonRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                       readFd, ret, errno, strerror(errno));
                
                // TODO: 如何处理？退出协程？
                // sleep 10 milisecond
                msleep(10);
            }
        }

        struct timeval t1,t2;
        gettimeofday(&t1, NULL);
        int count = 0;
        
        // 处理主题消息接收队列
        std::shared_ptr<TopicMessage> msg = queue.pop();
        while (msg) {
            auto iter = self->topicCallbackMap_.find(msg->topic);
            if (iter != self->topicCallbackMap_.end()) {
                for (auto it = iter->second.begin(); it != iter->second.end(); ++it) {
                    if (it->needCoroutine) {
                        TopicMessageTask *task = new TopicMessageTask();
                        task->callback = it->callback;
                        task->msg = msg;
                        RoutineEnvironment::startCoroutine(callbackRoutine, task);
                    } else {
                        it->callback(msg->topic, msg->message);
                    }
                }
            }
            
            // 防止其他协程（如：RoutineEnvironment::cleanRoutine）长时间不被调度，让其他协程处理一下
            count++;
            if (count == 100) {
                gettimeofday(&t2, NULL);
                if ((t2.tv_sec - t1.tv_sec) * 1000000 + t2.tv_usec - t1.tv_usec > 100000) {
                    msleep(1);
                    gettimeofday(&t1, NULL);
                }
                count = 0;
            }

            msg = queue.pop();
        }
    }
}

void *SubscribeEnv::callbackRoutine(void * arg) {
    TopicMessageTask *task = (TopicMessageTask*)arg;
    task->callback(task->msg->topic, task->msg->message);
    delete task;
    return NULL;
}