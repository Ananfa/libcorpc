//
//  corpc_mysql.h
//  corpc_mysql
//
//  Created by Xianke Liu on 2018/5/8.
//Copyright © 2018年 Dena. All rights reserved.
//

#ifndef corpc_mysql_h
#define corpc_mysql_h

#include "corpc_routine_env.h"
#include "corpc_controller.h"
#include "corpc_inner_rpc.h"

#include "corpc_thirdparty.pb.h"

#include <mysql.h>
#include <thread>
#include <list>

using namespace corpc;

class MysqlConnectPool : public thirdparty::ThirdPartyService {
public:
    MysqlConnectPool(std::string &host, std::string &user, std::string &passwd, std::string &db, unsigned int port, std::string &unix_socket, unsigned long clientflag, uint32_t maxConnectNum, uint32_t maxIdleNum): _host(host), _user(user), _passwd(passwd), _db(db), _port(port), _unix_socket(unix_socket), _clientflag(clientflag), _maxConnectNum(maxConnectNum), _maxIdleNum(maxIdleNum) {}
    ~MysqlConnectPool() {}
    virtual void take(::google::protobuf::RpcController* controller,
                     const Void* request,
                     thirdparty::Handle* response,
                      ::google::protobuf::Closure* done);
    virtual void put(::google::protobuf::RpcController* controller,
                     const thirdparty::Handle* request,
                     Void* response,
                     ::google::protobuf::Closure* done);
    
private:
    std::string _host;
    std::string _user;
    std::string _passwd;
    std::string _db;
    unsigned int _port;
    std::string _unix_socket;
    unsigned long _clientflag;
    
    uint32_t _maxConnectNum;    // 与mysql数据库最多建立的连接数
    uint32_t _maxIdleNum;       // 最大空闲连接数量
    uint32_t _realConnectCount; // 当前实际建立连接的数量
    
    std::list<MYSQL*> _idleList; // 空闲连接表
    std::list<stCoRoutine_t*> _waitingList; // 等待队列：当连接数量达到最大时，新的请求需要等待
};

#endif /* corpc_mysql_h */
