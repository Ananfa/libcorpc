//
//  corpc_inner_server.cpp
//  rpccli
//
//  Created by Xianke Liu on 2018/3/1.
//Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_routine_env.h"

#include "corpc_inner.h"

#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "corpc_option.pb.h"

namespace CoRpc {
    
    namespace Inner {
        __thread Client *Client::_instance(nullptr);
        
        void Client::Channel::CallMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller, const google::protobuf::Message *request, google::protobuf::Message *response, google::protobuf::Closure *done) {
            Request *req = new Request;
            req->client = _client;
            req->server = _server;
            req->co = co_self();
            req->request = request;
            req->response = response;
            req->controller = controller;
            req->serviceId = method->service()->options().GetExtension(corpc::global_service_id);
            req->methodId = method->index();
            
            _server->request(req);
            
            co_yield_ct(); // 等待rpc结果到来被唤醒继续执行
            
            delete req;
            
            // 正确返回
            if (done) {
                done->Run();
            }
        }
        
        Client* Client::instance() {
            if (!_instance) {
                _instance = new Client();
                _instance->start();
            }
            
            return _instance;
        }
        
        void Client::start() {
            RoutineEnvironment::startCoroutine(responseQueueRoutine, this);
        }
        
        void Client::response(stCoRoutine_t *co) {
            _queue.push(co);
        }
        
        void *Client::responseQueueRoutine( void * arg ) {
            Client *client = (Client*)arg;
            ResponseQueue& queue = client->_queue;
            
            // 初始化pipe readfd
            co_enable_hook_sys();
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
                        printf("ERROR: InnerServer::taskHandleRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                               readFd, ret, errno, strerror(errno));
                        
                        // TODO: 如何处理？退出协程？
                        // sleep 10 milisecond
                        usleep(10000);
                    }
                }
                
                // 处理任务队列
                stCoRoutine_t *co = queue.pop();
                while (co) {
                    co_resume(co);
                    
                    co = queue.pop();
                }
            }
            
            return NULL;
        }
        
        Server* Server::create() {
            Server *server = new Server();
            server->start();
            return server;
        }
        
        bool Server::registerService(::google::protobuf::Service *rpcService) {
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
        
        google::protobuf::Service *Server::getService(uint32_t serviceId) const {
            std::map<uint32_t, ServiceData>::const_iterator it = _services.find(serviceId);
            if (it == _services.end()) {
                return NULL;
            }
            
            return it->second.rpcService;
        }
        
        const MethodData *Server::getMethod(uint32_t serviceId, uint32_t methodId) const {
            std::map<uint32_t, ServiceData>::const_iterator it = _services.find(serviceId);
            if (it == _services.end()) {
                return NULL;
            }
            
            if (it->second.methods.size() <= methodId) {
                return NULL;
            }
            
            return &(it->second.methods[methodId]);
        }
        
        void Server::start() {
            RoutineEnvironment::startCoroutine(requestQueueRoutine, this);
        }
        
        void Server::request(Request *request) {
            _queue.push(request);
        }
        
        void *Server::requestQueueRoutine( void * arg ) {
            Server *server = (Server*)arg;
            
            RequestQueue& queue = server->_queue;
            
            // 初始化pipe readfd
            co_enable_hook_sys();
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
                        printf("ERROR: InnerServer::taskHandleRoutine read from pipe fd %d ret %d errno %d (%s)\n",
                               readFd, ret, errno, strerror(errno));
                        
                        // TODO: 如何处理？退出协程？
                        // sleep 10 milisecond
                        usleep(10000);
                    }
                }
                
                // 处理任务队列
                Request *request = queue.pop();
                while (request) {
                    const MethodData *methodData = server->getMethod(request->serviceId, request->methodId);
                    
                    bool needCoroutine = methodData->method_descriptor->options().GetExtension(corpc::need_coroutine);
                    
                    if (needCoroutine) {
                        // 启动协程进行rpc处理
                        RoutineEnvironment::startCoroutine(requestRoutine, request);
                    } else {
                        // rpc处理方法调用
                        server->getService(request->serviceId)->CallMethod(methodData->method_descriptor, request->controller, request->request, request->response, NULL);
                        
                        // 处理结果发回给Client
                        request->client->response(request->co);
                    }
                    
                    request = queue.pop();
                }
            }
            
            return NULL;
        }
        
        void *Server::requestRoutine( void * arg ) {
            Request *request = (Request*)arg;
            Server *server = request->server;
            
            const MethodData *methodData = server->getMethod(request->serviceId, request->methodId);
            
            server->getService(request->serviceId)->CallMethod(methodData->method_descriptor, request->controller, request->request, request->response, NULL);
            
            // 处理结果发回给Client
            request->client->response(request->co);
            
            return NULL;
        }
        
    }
    
}
