/*
 * Created by Xianke Liu on 2017/11/1.
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

#ifndef corpc_controller_h
#define corpc_controller_h

#include <google/protobuf/service.h>
#include <string>

namespace corpc {
    class Controller : public google::protobuf::RpcController {
        std::string error_str_;
        int32_t error_code_;
    public:
        Controller() { Reset(); }
        void Reset() {
            error_str_ = "";
            error_code_ = 0;
        }
        bool Failed() const {
            return error_code_ != 0;
        }
        std::string ErrorText() const {
            return error_str_;
        }
        void StartCancel() { // NOT IMPL
            return ;
        }
        void SetFailed(const std::string &reason) {
            error_str_ = reason;
            if (error_code_ == 0) {
                error_code_ = -1;
            }
        }
        bool IsCanceled() const { // NOT IMPL
            return false;
        }
        void SetErrorCode(int32_t error_code) {
            error_code_ = error_code;
        }
        int32_t GetErrorCode() {
            return error_code_;
        }
        
        void NotifyOnCancel(google::protobuf::Closure* callback) { // NOT IMPL
            return;
        }
    };
    
}

#endif /* corpc_controller_h */
