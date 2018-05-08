//
//  corpc_mysql.cpp
//  corpc_mysql
//
//  Created by Xianke Liu on 2018/5/8.
//Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_mysql.h"

using namespace corpc;

void MysqlConnectPool::take(::google::protobuf::RpcController* controller,
                            const Void* request,
                            thirdparty::Handle* response,
                            ::google::protobuf::Closure* done) {
    if (_idleList.size() > 0) {
        intptr_t handle = (intptr_t)_idleList.front();
        _idleList.pop_front();
        
        response->set_handle(handle);
    } else if (_realConnectCount < _maxConnectNum) {
        // 建立新连接
        MYSQL *con = mysql_init(NULL);
        
        if (mysql_real_connect(con, _host.c_str(), _user.c_str(), _passwd.c_str(), _db.c_str(), _port, _unix_socket.c_str(), _clientflag)) {
            response->set_handle((intptr_t)con);
            _realConnectCount++;
        } else {
            mysql_close(con);
            controller->SetFailed("can't connect to mysql server");
        }
    } else {
        // 等待空闲连接
        _waitingList.push_back(co_self());
        co_yield_ct();
        
        assert(_idleList.size() > 0);
        intptr_t handle = (intptr_t)_idleList.front();
        _idleList.pop_front();
        
        response->set_handle(handle);
    }
}

void MysqlConnectPool::put(::google::protobuf::RpcController* controller,
                           const thirdparty::Handle* request,
                           Void* response,
                           ::google::protobuf::Closure* done) {
    intptr_t handle = (intptr_t)request->handle();
    
    if (_idleList.size() < _maxIdleNum) {
        _idleList.push_back((MYSQL*)handle);
        
        if (_waitingList.size() > 0) {
            stCoRoutine_t *co = _waitingList.front();
            _waitingList.pop_front();
            
            co_resume(co);
        }
    } else {
        assert(_waitingList.size() == 0);
        _realConnectCount--;
        mysql_close((MYSQL*)handle);
    }
}
