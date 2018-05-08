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
        std::string _error_str;
        bool _is_failed;
    public:
        Controller() { Reset(); }
        void Reset() {
            _error_str = "";
            _is_failed = false;
        }
        bool Failed() const {
            return _is_failed;
        }
        std::string ErrorText() const {
            return _error_str;
        }
        void StartCancel() { // NOT IMPL
            return ;
        }
        void SetFailed(const std::string &reason) {
            _is_failed = true;
            _error_str = reason;
        }
        bool IsCanceled() const { // NOT IMPL
            return false;
        }
        
        void NotifyOnCancel(google::protobuf::Closure* callback) { // NOT IMPL
            return;
        }
    };
    
}

#endif /* corpc_controller_h */
