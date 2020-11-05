/*
 * Created by Xianke Liu on 2018/3/1.
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

#include "corpc_routine_env.h"

#include "corpc_inner_rpc.h"
#include "corpc_utils.h"
#include "corpc_controller.h"

#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "corpc_option.pb.h"
#include <sys/time.h>

using namespace corpc;

void InnerRpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller, const google::protobuf::Message *request, google::protobuf::Message *response, google::protobuf::Closure *done) {
    InnerRpcRequest *req = new InnerRpcRequest;
    req->server = _server;
    req->rpcTask = std::make_shared<RpcClientTask>();
    req->rpcTask->pid = GetPid();
    req->rpcTask->co = co_self();
    req->rpcTask->request = request;
    req->rpcTask->controller = controller;
    req->rpcTask->done = done;
    req->rpcTask->serviceId = method->service()->options().GetExtension(corpc::global_service_id);
    req->rpcTask->methodId = method->index();
    
    bool care_response = !method->options().GetExtension(corpc::not_care_response);
    uint32_t timeout = method->options().GetExtension(corpc::timeout);
    assert(care_response || done); // not_care_response need done
    if (care_response) {
        assert(response != NULL);
        req->rpcTask->response = response;
        if (timeout > 0) {
            req->rpcTask->response_1 = response->New();
            req->rpcTask->request_1 = request->New();
            req->rpcTask->request_1->CopyFrom(*request);
            req->rpcTask->controller_1 = new Controller();

            struct timeval t;
            gettimeofday(&t, NULL);
            uint64_t now = t.tv_sec * 1000 + t.tv_usec / 1000;
            req->rpcTask->expireTime = now + timeout;
            RoutineEnvironment::addTimeoutTask(req->rpcTask);
        } else {
            req->rpcTask->response_1 = NULL;
            req->rpcTask->request_1 = NULL;
            req->rpcTask->controller_1 = NULL;
            req->rpcTask->expireTime = 0;
        }
    } else {
        assert(timeout == 0);
        req->rpcTask->response = NULL;
        req->rpcTask->response_1 = NULL;
        req->rpcTask->request_1 = NULL;
        req->rpcTask->controller_1 = NULL;
        req->rpcTask->expireTime = 0;
    }

    _server->_queue.push(req);
    
    if (care_response) {
        co_yield_ct(); // 等待rpc结果到来被唤醒继续执行
        
        // 正确返回
        if (done) {
            done->Run();
        }
    }
}

InnerRpcServer* InnerRpcServer::create(bool startInNewThread) {
    InnerRpcServer *server = new InnerRpcServer();
    server->start(startInNewThread);
    return server;
}

bool InnerRpcServer::registerService(::google::protobuf::Service *rpcService) {
    const google::protobuf::ServiceDescriptor *serviceDescriptor = rpcService->GetDescriptor();
    
    uint32_t serviceId = (uint32_t)(serviceDescriptor->options().GetExtension(corpc::global_service_id));
    
    std::map<uint32_t, ServiceData>::iterator it = _services.find(serviceId);
    if (it != _services.end()) {
        return false;
    }
    
    ServiceData &serviceData = _services[serviceId];
    serviceData.rpcService = rpcService;
    
    MethodData methodData;
    for (int i = 0; i < serviceDescriptor->method_count(); i++) {
        methodData.method_descriptor = serviceDescriptor->method(i);
        methodData.request_proto = &rpcService->GetRequestPrototype(methodData.method_descriptor);
        methodData.response_proto= &rpcService->GetResponsePrototype(methodData.method_descriptor);
        
        serviceData.methods.push_back(methodData);
    }
    
    return true;
}

google::protobuf::Service *InnerRpcServer::getService(uint32_t serviceId) const {
    std::map<uint32_t, ServiceData>::const_iterator it = _services.find(serviceId);
    if (it == _services.end()) {
        return NULL;
    }
    
    return it->second.rpcService;
}

const MethodData *InnerRpcServer::getMethod(uint32_t serviceId, uint32_t methodId) const {
    std::map<uint32_t, ServiceData>::const_iterator it = _services.find(serviceId);
    if (it == _services.end()) {
        return NULL;
    }
    
    if (it->second.methods.size() <= methodId) {
        return NULL;
    }
    
    return &(it->second.methods[methodId]);
}

void InnerRpcServer::start(bool startInNewThread) {
    if (startInNewThread) {
        _t = std::thread(threadEntry, this);
    } else {
        RoutineEnvironment::startCoroutine(requestQueueRoutine, this);
    }
}

void InnerRpcServer::threadEntry( InnerRpcServer * self ) {
    // 启动rpc任务处理协程
    RoutineEnvironment::startCoroutine(requestQueueRoutine, self);
    
    RoutineEnvironment::runEventLoop();
}

void *InnerRpcServer::requestQueueRoutine( void * arg ) {
    InnerRpcServer *server = (InnerRpcServer*)arg;
    
    InnerRpcRequestQueue& queue = server->_queue;
    
    // 初始化pipe readfd
    int readFd = queue.getReadFd();
    co_register_fd(readFd);
    co_set_timeout(readFd, -1, 1000);
    
    int ret;
    std::vector<char> buf(1024);
    while (true) {
        // 等待处理信号
        ret = read(readFd, &buf[0], 1024);
        assert(ret != 0);
        if (ret < 0) {
            if (errno == EAGAIN) {
                continue;
            } else {
                // 管道出错
                ERROR_LOG("InnerServer::taskHandleRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                       readFd, ret, errno, strerror(errno));
                
                // TODO: 如何处理？退出协程？
                // sleep 10 milisecond
                msleep(10);
            }
        }
        
        struct timeval t1,t2;
        gettimeofday(&t1, NULL);
        uint64_t now = t1.tv_sec * 1000 + t1.tv_usec / 1000; // 当前时间（毫秒精度）
        int count = 0;
        
        // 处理任务队列
        InnerRpcRequest *request = queue.pop();
        while (request) {
            if (request->rpcTask->expireTime != 0 && now >= request->rpcTask->expireTime) {
                assert(request->rpcTask->response);
                // 超时
                RoutineEnvironment::resumeCoroutine(request->rpcTask->pid, request->rpcTask->co, request->rpcTask->expireTime, ETIMEDOUT);

                delete request;
            } else {
                const MethodData *methodData = server->getMethod(request->rpcTask->serviceId, request->rpcTask->methodId);
                
                bool needCoroutine = methodData->method_descriptor->options().GetExtension(corpc::need_coroutine);
                
                if (needCoroutine) {
                    // 启动协程进行rpc处理
                    RoutineEnvironment::startCoroutine(requestRoutine, request);
                } else {
                    // rpc处理方法调用
                    if (request->rpcTask->expireTime == 0) {
                        server->getService(request->rpcTask->serviceId)->CallMethod(methodData->method_descriptor, request->rpcTask->controller, request->rpcTask->request, request->rpcTask->response, NULL);
                    } else {
                        server->getService(request->rpcTask->serviceId)->CallMethod(methodData->method_descriptor, request->rpcTask->controller_1, request->rpcTask->request_1, request->rpcTask->response_1, NULL);
                    }
                    
                    if (request->rpcTask->response) {
                        // 唤醒协程处理结果
                        RoutineEnvironment::resumeCoroutine(request->rpcTask->pid, request->rpcTask->co, request->rpcTask->expireTime);
                    } else {
                        // not_care_response类型的rpc需要在这里触发回调清理request
                        assert(request->rpcTask->done);
                        request->rpcTask->done->Run();
                    }
                    
                    delete request;
                }
            }


            // 防止其他协程（如：RoutineEnvironment::cleanRoutine）长时间不被调度，让其他协程处理一下
            count++;
            if (count == 100) {
                gettimeofday(&t2, NULL);
                if ((t2.tv_sec - t1.tv_sec) * 1000000 + t2.tv_usec - t1.tv_usec > 100000) {
                    msleep(1);
                    
                    gettimeofday(&t1, NULL);
                    now = t1.tv_sec * 1000 + t1.tv_usec / 1000;
                }
                count = 0;
            }
            
            request = queue.pop();
        }
    }
    
    return NULL;
}

void *InnerRpcServer::requestRoutine( void * arg ) {
    InnerRpcRequest *request = (InnerRpcRequest*)arg;
    InnerRpcServer *server = request->server;
    
    const MethodData *methodData = server->getMethod(request->rpcTask->serviceId, request->rpcTask->methodId);
    
    if (request->rpcTask->expireTime == 0) {
        server->getService(request->rpcTask->serviceId)->CallMethod(methodData->method_descriptor, request->rpcTask->controller, request->rpcTask->request, request->rpcTask->response, NULL);
    } else {
        server->getService(request->rpcTask->serviceId)->CallMethod(methodData->method_descriptor, request->rpcTask->controller_1, request->rpcTask->request_1, request->rpcTask->response_1, NULL);
    }
    
    if (request->rpcTask->response) {
        // 唤醒协程处理结果
        RoutineEnvironment::resumeCoroutine(request->rpcTask->pid, request->rpcTask->co, request->rpcTask->expireTime);
    } else {
        // not_care_response类型的rpc需要在这里触发回调清理request
        assert(request->rpcTask->done);
        request->rpcTask->done->Run();
    }
    
    delete request;
    
    return NULL;
}
