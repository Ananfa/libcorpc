/*
 * Created by Xianke Liu on 2020/7/1.
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

#include "co_routine.h"
#include "co_routine_inner.h"
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#ifndef corpc_rpc_common_h
#define corpc_rpc_common_h

namespace corpc {
    class RpcClientTask {
    public:
        pid_t pid;
        stCoRoutine_t *co;
        const google::protobuf::Message* request;
        google::protobuf::Message* request_1;
        google::protobuf::Message* response;
        google::protobuf::Message* response_1;
        google::protobuf::RpcController *controller;
        google::protobuf::RpcController *controller_1;
        google::protobuf::Closure *done;
        uint32_t serviceId;
        uint32_t methodId;
        uint64_t expireTime;

    public:
        ~RpcClientTask() {
            if (expireTime > 0) {
                delete response_1;
                delete request_1;
                delete controller_1;
            }
        }
    };

    class RpcServerTask {
    public:
        google::protobuf::Service *service;
        const google::protobuf::MethodDescriptor *method_descriptor;
        const google::protobuf::Message* request;
        google::protobuf::Message* response;
        google::protobuf::RpcController *controller;
        google::protobuf::Closure *done;
        uint64_t callId;
        uint64_t expireTime;

    public:
        RpcServerTask(): service(NULL), method_descriptor(NULL), request(NULL), response(NULL), controller(NULL), done(NULL) {}
        ~RpcServerTask() {
            delete response;

            if (done == NULL) {
                delete request;
                delete controller;
            }
        }
    };
    
}

#endif /* corpc_rpc_common_h */