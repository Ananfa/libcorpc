/*
 * Created by Xianke Liu on 2017/11/21.
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
#include "corpc_rpc_server.h"
#include "corpc_controller.h"
#include "corpc_utils.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <sys/time.h>

#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "corpc_option.pb.h"

// TODO: 使用统一的Log接口记录Log

namespace corpc {
    
    RpcServer::Connection::Connection(int fd, RpcServer* server): corpc::Connection(fd, server->_io, false), _server(server) {
    }
    
    RpcServer::Connection::~Connection() {
        LOG("INFO: RpcServer::Connection::~Connection -- fd:%d in thread:%d\n", _fd, GetPid());
    }
    
    void RpcServer::Connection::onClose() {
        std::shared_ptr<corpc::Connection> self = corpc::Connection::shared_from_this();
        _server->onClose(self);
    }
    
    void *RpcServer::MultiThreadWorker::taskCallRoutine( void * arg ) {
        WorkerTask *task = (WorkerTask *)arg;
        
        task->rpcTask->service->CallMethod(task->rpcTask->method_descriptor, task->rpcTask->controller, task->rpcTask->request, task->rpcTask->response, NULL);
        
        if (task->rpcTask->response != NULL) {
            // 处理结果发给sender处理
            task->connection->send(task->rpcTask);
        }
        
        delete task;
        
        return NULL;
    }
    
    void RpcServer::MultiThreadWorker::handleMessage(void *msg) {
        WorkerTask *task = (WorkerTask *)msg;
        
        bool needCoroutine = task->rpcTask->method_descriptor->options().GetExtension(corpc::need_coroutine);
        
        if (needCoroutine) {
            // 启动协程进行rpc处理
            RoutineEnvironment::startCoroutine(taskCallRoutine, task);
        } else {
            // rpc处理方法调用
            task->rpcTask->service->CallMethod(task->rpcTask->method_descriptor, task->rpcTask->controller, task->rpcTask->request, task->rpcTask->response, NULL);
            
            if (task->rpcTask->response != NULL) {
                // 处理结果发给sender处理
                task->connection->send(task->rpcTask);
            }
            
            delete task;
        }
    }
    
    void *RpcServer::CoroutineWorker::taskCallRoutine( void * arg ) {
        WorkerTask *task = (WorkerTask *)arg;
        
        task->rpcTask->service->CallMethod(task->rpcTask->method_descriptor, task->rpcTask->controller, task->rpcTask->request, task->rpcTask->response, NULL);
        
        if (task->rpcTask->response != NULL) {
            // 处理结果发给sender处理
            task->connection->send(task->rpcTask);
        }
        
        delete task;
        
        return NULL;
    }
    
    void RpcServer::CoroutineWorker::handleMessage(void *msg) {
        WorkerTask *task = (WorkerTask *)msg;
        
        bool needCoroutine = task->rpcTask->method_descriptor->options().GetExtension(corpc::need_coroutine);
        
        if (needCoroutine) {
            // 启动协程进行rpc处理
            RoutineEnvironment::startCoroutine(taskCallRoutine, task);
        } else {
            // rpc处理方法调用
            task->rpcTask->service->CallMethod(task->rpcTask->method_descriptor, task->rpcTask->controller, task->rpcTask->request, task->rpcTask->response, NULL);
            
            if (task->rpcTask->response != NULL) {
                // 处理结果发给sender处理
                task->connection->send(task->rpcTask);
            }
            
            delete task;
        }
    }
    
    RpcServer::RpcServer(IO *io, uint16_t workThreadNum, const std::string& ip, uint16_t port): corpc::Server(io) {
        _acceptor = new TcpAcceptor(this, ip, port);

        // 根据需要创建多线程worker或协程worker
        if (workThreadNum > 0) {
            _worker = new MultiThreadWorker(this, workThreadNum);
        } else {
            _worker = new CoroutineWorker(this);
        }
        
        _pipelineFactory = new TcpPipelineFactory(_worker, decode, encode, CORPC_REQUEST_HEAD_SIZE, CORPC_MAX_REQUEST_SIZE, 0, corpc::MessagePipeline::FOUR_BYTES);
    }
    
    RpcServer::~RpcServer() {}
    
    RpcServer* RpcServer::create(IO *io, uint16_t workThreadNum, const std::string& ip, uint16_t port) {
        assert(io);
        RpcServer *server = new RpcServer(io, workThreadNum, ip, port);
        
        server->start();
        return server;
    }
    
    void * RpcServer::decode(std::shared_ptr<corpc::Connection> &connection, uint8_t *head, uint8_t *body, int size) {
        std::shared_ptr<Connection> conn = std::static_pointer_cast<Connection>(connection);
        
        RpcServer *server = conn->getServer();
        
        uint32_t reqSize = *(uint32_t *)head;
        reqSize = be32toh(reqSize);
        uint32_t serviceId = *(uint32_t *)(head + 4);
        serviceId = be32toh(serviceId);
        uint32_t methodId = *(uint32_t *)(head + 8);
        methodId = be32toh(methodId);
        uint64_t callId = *(uint64_t *)(head + 12);
        callId = be64toh(callId);
        uint64_t expireTime = *(uint64_t *)(head + 20);
        expireTime = be64toh(expireTime);
        
        // 生成ServerRpcTask
        // 根据serverId和methodId查表
        const MethodData *methodData = server->getMethod(serviceId, methodId);
        if (methodData != NULL) {
            const google::protobuf::MethodDescriptor *method_descriptor = methodData->method_descriptor;
            
            google::protobuf::Message *request = methodData->request_proto->New();
            google::protobuf::Message *response = method_descriptor->options().GetExtension(corpc::not_care_response) ? NULL : methodData->response_proto->New();
            Controller *controller = new Controller();
            if (!request->ParseFromArray(body, size)) {
                // 出错处理
                ERROR_LOG("RpcServer::decode -- parse request body fail\n");
                delete request;
                delete response;
                delete controller;
                
                return nullptr;
            }
            
            // 将收到的请求传给worker
            WorkerTask *task = new WorkerTask;
            task->connection = conn;
            task->rpcTask = std::shared_ptr<RpcServerTask>(new RpcServerTask);
            task->rpcTask->service = server->getService(serviceId);
            task->rpcTask->method_descriptor = method_descriptor;
            task->rpcTask->request = request;
            task->rpcTask->response = response;
            task->rpcTask->controller = controller;
            task->rpcTask->callId = callId;
            task->rpcTask->expireTime = expireTime;
            
            return task;
        } else {
            // 出错处理
            ERROR_LOG("RpcServer::decode -- can't find method object of serviceId: %u methodId: %u\n", serviceId, methodId);
            
            return nullptr;
        }
    }
    
    bool RpcServer::encode(std::shared_ptr<corpc::Connection> &connection, std::shared_ptr<void>& data, uint8_t *buf, int space, int &size, std::string &downflowBuf, uint32_t &downflowBufSentNum) {
        std::shared_ptr<RpcServerTask> rpcTask = std::static_pointer_cast<RpcServerTask>(data);
        uint32_t msgSize = rpcTask->response->GetCachedSize();
        if (msgSize == 0) {
            msgSize = rpcTask->response->ByteSize();
        }
        
        // 若空间不足容纳消息头部则等待下次
        if (CORPC_RESPONSE_HEAD_SIZE > space) {
            return true;
        }
        
        *(uint32_t *)buf = htobe32(msgSize);
        *(uint64_t *)(buf + 4) = htobe64(rpcTask->callId);
        *(uint64_t *)(buf + 12) = htobe64(rpcTask->expireTime);
        
        int spaceleft = space - CORPC_RESPONSE_HEAD_SIZE;
        if (spaceleft >= msgSize) {
            if (msgSize > 0) {
                rpcTask->response->SerializeWithCachedSizesToArray(buf + CORPC_RESPONSE_HEAD_SIZE);
            }
            
            size = CORPC_RESPONSE_HEAD_SIZE + msgSize;
        } else {
            downflowBuf.assign(msgSize, 0);
            uint8_t *dbuf = (uint8_t*)downflowBuf.data();
            rpcTask->response->SerializeWithCachedSizesToArray(dbuf);
            
            if (spaceleft > 0) {
                memcpy(buf + CORPC_RESPONSE_HEAD_SIZE, dbuf, spaceleft);
                downflowBufSentNum = spaceleft;
            }
            
            size = space;
        }
        
        return true;
    }
    
    bool RpcServer::registerService(::google::protobuf::Service *rpcService) {
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
    
    google::protobuf::Service *RpcServer::getService(uint32_t serviceId) const {
        std::map<uint32_t, ServiceData>::const_iterator it = _services.find(serviceId);
        if (it == _services.end()) {
            return NULL;
        }
        
        return it->second.rpcService;
    }
    
    const MethodData *RpcServer::getMethod(uint32_t serviceId, uint32_t methodId) const {
        std::map<uint32_t, ServiceData>::const_iterator it = _services.find(serviceId);
        if (it == _services.end()) {
            return NULL;
        }
        
        if (it->second.methods.size() <= methodId) {
            return NULL;
        }
        
        return &(it->second.methods[methodId]);
    }
    
    corpc::Connection *RpcServer::buildConnection(int fd) {
        return new Connection(fd, this);
    }
    
    void RpcServer::onClose(std::shared_ptr<corpc::Connection>& connection) {
        LOG("RpcServer::onClose -- connection fd:%d is closed\n", connection->getfd());
    }
    
}
