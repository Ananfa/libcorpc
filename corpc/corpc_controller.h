//
//  co_rpc_controller.h
//  rpccli
//
//  Created by Xianke Liu on 2017/11/1.
//  Copyright © 2017年 Dena. All rights reserved.
//

#ifndef corpc_controller_h
#define corpc_controller_h

#include <google/protobuf/service.h>
#include <string>

namespace CoRpc {
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
