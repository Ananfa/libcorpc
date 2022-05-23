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

#ifndef corpc_pubsub_h
#define corpc_pubsub_h

#include "corpc_io.h"
#include "corpc_redis.h"

#include <list>
#include <map>
#include <string>
#include <hiredis.h>

namespace corpc {
    // 由于在libcorpc中只能使用hiredis的同步模式，在同步模式下无法中途取消订阅
    // 由于redisContext对象不是线程安全，对redisContext的订阅操作只能在同一协程中同步执行
    // 因此，该订阅发布服务只能用于服务器级别的静态订阅，即服务器启动时订阅所有需要的主题，中途只能增加主题的回调注册
    
    class SubscribeEnv;

    typedef std::function<void(const std::string& topic, const std::string& msg)> SubcribeCallback;

    struct SubscribeCallbackInfo {
        bool needCoroutine;
        SubcribeCallback callback;
    };

    struct TopicMessage {
        std::string topic;
        std::string message;
    };

#ifdef USE_NO_LOCK_QUEUE
    typedef Co_MPSC_NoLockQueue<std::shared_ptr<TopicMessage>> TopicMessageQueue;
#else
    typedef CoSyncQueue<std::shared_ptr<TopicMessage>> TopicMessageQueue;
#endif

    struct TopicMessageTask {
        SubcribeCallback callback;
        std::shared_ptr<TopicMessage> msg;
    };

    struct TopicRegisterMessage {
        std::string topic;
        SubscribeEnv *env;
    };

#ifdef USE_NO_LOCK_QUEUE
    typedef Co_MPSC_NoLockQueue<TopicRegisterMessage*> TopicRegisterMessageQueue;
#else
    typedef CoSyncQueue<TopicRegisterMessage*> TopicRegisterMessageQueue;
#endif

    class PubsubService {
    public:
        // 要使用发布订阅服务需要先进行初始化，创建服务单例对象。启动处理线程，并在处理线程中启动发布协程，若有订阅主题则启动订阅协程
        static bool StartPubsubService(RedisConnectPool *redisPool); 
        static bool Subscribe(const std::string& topic, bool needCoroutine, SubcribeCallback callback); // 注意：只能订阅已有的主题，回调方法会在调用该Subscribe的线程中执行
        static bool Publish(const std::string& topic, const std::string& msg); // 发布主题消息

    private:
        PubsubService(RedisConnectPool *redisPool);
        ~PubsubService() {}

        void _start();
        bool _subscribe(const std::string& topic, bool needCoroutine, SubcribeCallback callback);
        bool _publish(const std::string& topic, const std::string& msg);
        // FixMe: 是否需要提供取消订阅功能？

        static void threadEntry(PubsubService *self); // 服务线程
        static void *registerRoutine(void *arg);  // 登记订阅协程
        static void *subscribeRoutine(void *arg);  // 订阅协程

    private:
        static PubsubService* _service;

        RedisConnectPool *_redisPool;

        PipeType _pipe; // 用于通知有新主题需要订阅
        std::thread _t; // 任务处理线程

        redisContext *_subCon; // 订阅连接

        // 订阅登记队列（registerRoutine处理此队列）
        TopicRegisterMessageQueue _queue;

        // 订阅回调关系表（登记主题与订阅回调环境的关系）
        std::map<std::string, std::list<SubscribeEnv*>> _topicToEnvsMap;

    public:
        friend class SubscribeEnv;
    };

    // 线程相关的订阅回调环境
    // 注意：当前SubscribeEnv的实现不支持销毁（采用libcorpc开发服务器时线程数是设定的，应通过动态启停协程来实现功能）
    class SubscribeEnv {
    private:
        SubscribeEnv() {}
        ~SubscribeEnv() {}

        static SubscribeEnv *getEnv();
    
        static void *deamonRoutine(void *arg); // 主题消息接收队列处理协程
        static void *callbackRoutine(void * arg); // 回调处理协程

        void initialize();

        void addSubscribeCallback(const std::string& topic, bool needCoroutine, SubcribeCallback callback);

    private:
        std::map<std::string, std::list<SubscribeCallbackInfo>> _topicCallbackMap; // 主题回调函数表

        TopicMessageQueue _queue; // 主题消息接收队列

    public:
        friend class PubsubService;
    };
}

#endif /* corpc_pubsub_h */