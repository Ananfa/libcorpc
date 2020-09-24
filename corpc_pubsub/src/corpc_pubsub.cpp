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
#include <string.h>

using namespace corpc;

PubsubService* PubsubService::_service = nullptr;

PubsubService::PubsubService(RedisConnectPool *redisPool, const std::list<const std::string topic>& subTopics): _redisPool(redisPool), _subCon(nullptr), _pubCon(nullptr) {
    for (auto it = subTopics.cbegin(); it != subTopics.cend(); ++it) {
        _subTopicMap[*it] = true;
    }
}

bool PubsubService::StartPubsubService(RedisConnectPool *redisPool, const std::list<const std::string topic>& subTopics) {
    if (_service != nullptr) {
        return false;
    }

    _service = new PubsubService(redisPool, subTopics);
    _service->start();

    return true;
}

bool PubsubService::Subscribe(const std::string& topic, bool needCoroutine, SubcribeCallback callback) {
    if (_service == nullptr) {
        return false;
    }

    return _service->_subscribe(topic, needCoroutine, callback);
}

bool PubsubService::Publish(const std::string& topic, const std::string& msg) {
    if (_service == nullptr) {
        return false;
    }

    return _service->_publish(topic, msg);
}

void PubsubService::threadEntry(PubsubService *self) {
    RoutineEnvironment::startCoroutine(subscribeRoutine, self);
    RoutineEnvironment::startCoroutine(registerRoutine, self);
    RoutineEnvironment::runEventLoop();
}

void *PubsubService::subscribeRoutine(void *arg) {
    PubsubService *self = (PubsubService *)arg;
    
    std::string topicStr;
    for (auto it = self->_subTopics.cbegin(); it != self->_subTopics.cend(); ++it) {
        topicStr += *it;
        topicStr += " ";
    }

    RedisConnectPool::Proxy* proxy = self->_redisPool->proxy;
    // Get redisContext from pool
    while (true) {
        redisContext *redis = proxy->take();
        redisReply *reply = (redisReply *)redisCommand(redis, "SUBSCRIBE %s", topicStr.c_str());
        freeReplyObject(reply);
        while (redisGetReply(redis, &reply) == REDIS_OK) {
            // 向订阅主题环境发消息
            assert(reply->elements == 3);
            if (strcmp(reply->element[0]->str, "message") == 0) {
                auto it = _topicToEnvsMap.find(reply->element[1]->str);
                if (it != _topicToEnvsMap.end()) {
                    std::shared_ptr<TopicMessage> tmsg(new TopicMessage());
                    tmsg->topic.assign(reply->element[1]->str, reply->element[1]->len);
                    tmsg->message.assign(reply->element[2]->str, reply->element[2]->len);
                    for (auto it1 = it->begin(); it1 != it->end(); ++it1) {
                        it1->_queue.push(tmsg);
                    }
                }
            }
            
            freeReplyObject(reply);
        }
        proxy->put(redis, true);
        // 出错后等待1秒重连
        sleep(1);
    }
}

void *PubsubService::registerRoutine(void *arg) {
    PubsubService *self = (PubsubService *)arg;

    TopicRegisterMessageQueue& queue = self->_queue;

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
            _topicToEnvsMap[msg->topic].push_back(msg->env);
            delete msg;
            msg = queue.pop();
        }
    }
}

void PubsubService::start() {
    if (_subTopics.size() > 0) {
        // 启动处理线程
        _t = std::thread(threadEntry, this);
    }
}

bool PubsubService::_subscribe(const std::string& topic, bool needCoroutine, SubcribeCallback callback) {
    if (_service == nullptr) {
        ERROR_LOG("PubsubService::_subscribe -- service not start\n");
        return false;
    }

    // 判断主题是否在主题表中
    auto it = self->_subTopicMap.find(*it);
    if (it == self->_subTopicMap.end()) {
        ERROR_LOG("PubsubService::_subscribe -- topic invalid\n");
        return false;
    }

    // 注册订阅回调
    SubscribeEnv::getEnv()->addSubscribeCallback(topic, needCoroutine, callback);
    return true;
}

bool PubsubService::_publish(const std::string& topic, const std::string& msg) {
    RedisConnectPool::Proxy* proxy = _redisPool->proxy;
    redisContext *redis = proxy->take();
    if (redis == nullptr) {
        return false;
    }

    redisReply *reply = (redisReply *)redisCommand(redis, "PUBLISH %s %b", topic.c_str(), msg.c_str(), msg.size());
    if (reply == nullptr || redis->err) {
        if (reply != nullptr) {
            freeReplyObject(reply);
        }

        proxy->put(redis, true);
    } else {
        proxy->put(redis, false);
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
    _topicCallbackMap[topic].push_back({needCoroutine, callback});

    // 若第一次订阅主题，将主题和订阅队列传给_service线程的登记协程
    if (_topicCallbackMap[topic].size() == 1) {
        TopicRegisterMessage *msg = new TopicRegisterMessage();
        msg->topic = topic;
        msg->env = this;
        _service->_queue.push(msg);
    }
}

void *SubscribeEnv::deamonRoutine(void *arg) {
    SubscribeEnv *self = (SubscribeEnv *)arg;

    TopicMessageQueue& queue = self->_queue;

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
            auto iter = self->_topicCallbackMap.find(msg->topic);
            if (iter != self->_topicCallbackMap.end()) {
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
}