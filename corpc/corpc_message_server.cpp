//
//  corpc_message_server.cpp
//  corpc
//
//  Created by Xianke Liu on 2018/4/23.
//Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_routine_env.h"
#include "corpc_message_server.h"

namespace CoRpc {
    
    MessageServer::Connection::Connection(int fd, MessageServer* server): CoRpc::Connection(fd, server->_io), _server(server) {
    }
    
    MessageServer::Connection::~Connection() {
        printf("INFO: MessageServer::Connection::~Connection -- fd:%d\n", _fd);
    }
    
    void MessageServer::Connection::onClose() {
        std::shared_ptr<CoRpc::Connection> self = CoRpc::Connection::shared_from_this();
        _server->onClose(self);
    }
    
    void * MessageServer::Worker::taskCallRoutine( void * arg ) {
        WorkerTask *task = (WorkerTask *)arg;
        
        std::shared_ptr<MessageServer::Connection> conn = std::static_pointer_cast<MessageServer::Connection>(task->connection);
        
        MessageServer *server = static_cast<MessageServer *>(conn->getServer());
        
        auto iter = server->_registerMessageMap.find(task->type);
        
        iter->second.handle(task->msg, task->connection);
        
        delete task;
        
        return NULL;
    }
    
    void MessageServer::Worker::handleMessage(void *msg) {
        // 注意：处理完消息需要自己删除msg
        WorkerTask *task = (WorkerTask *)msg;
        
        switch (task->type) {
            case -1: // 新连接建立
                // TODO:
                break;
            case -2: // 连接断开
                // TODO:
                break;
            default: {
                assert(task->type > 0);
                // 其他消息处理
                auto iter = _server->_registerMessageMap.find(task->type);
                if (iter == _server->_registerMessageMap.end()) {
                    printf("ERROR: TestTcpServer::Worker::handleMessage -- unknown msg type\n");
                    
                    return;
                }
                
                if (iter->second.needCoroutine) {
                    CoRpc::RoutineEnvironment::startCoroutine(taskCallRoutine, task);
                } else {
                    iter->second.handle(task->msg, task->connection);
                    
                    delete task;
                }
                
                break;
            }
        }
    }
    
    MessageServer::MessageServer( IO *io): CoRpc::Server(io) {
        _worker = new Worker(this);
    }

    MessageServer::~MessageServer() {}
    
    bool MessageServer::registerMessage(int type,
                                        google::protobuf::Message *proto,
                                        bool needCoroutine,
                                        MessageHandle handle) {
        RegisterMessageInfo info;
        info.type = type;
        info.proto = proto;
        info.needCoroutine = info.needCoroutine;
        info.handle = handle;
        
        _registerMessageMap.insert(std::make_pair(type, info));
        
        return false;
    }
    
    bool MessageServer::start() {
        _worker->start();
        
        return true;
    }
    
    CoRpc::Connection *MessageServer::buildConnection(int fd) {
        return new Connection(fd, this);
    }
    
    void* MessageServer::decode(std::shared_ptr<CoRpc::Connection> &connection, uint8_t *head, uint8_t *body, int size) {
        std::shared_ptr<Connection> conn = std::static_pointer_cast<Connection>(connection);
        
        MessageServer *server = conn->getServer();
        
        uint32_t bodySize = *(uint32_t *)head;
        bodySize = ntohl(bodySize);
        int32_t msgType = *(int32_t *)(head + 4);
        msgType = ntohl(msgType);
        
        auto iter = server->_registerMessageMap.find(msgType);
        if (iter == server->_registerMessageMap.end()) {
            printf("ERROR: TestTcpServer::decode -- unknown msg type\n");
            
            return nullptr;
        }
        
        google::protobuf::Message *msg = iter->second.proto->New();
        if (!msg->ParseFromArray(body, size)) {
            // 出错处理
            printf("ERROR: TestTcpServer::decode -- parse msg body fail\n");
            
            return nullptr;
        }
        
        WorkerTask *task = new WorkerTask;
        task->type = msgType;
        task->connection = connection;
        task->msg = std::shared_ptr<google::protobuf::Message>(msg);
        
        return task;
    }
    
    bool MessageServer::encode(std::shared_ptr<CoRpc::Connection> &connection, std::shared_ptr<void>& data, uint8_t *buf, int space, int &size) {
        std::shared_ptr<SendMessageInfo> msgInfo = std::static_pointer_cast<SendMessageInfo>(data);
        uint32_t msgSize;
        if (msgInfo->isRaw) {
            std::shared_ptr<std::string> msg = std::static_pointer_cast<std::string>(msgInfo->msg);
            
            msgSize = msg->size();
            if (msgSize + CORPC_MESSAGE_HEAD_SIZE >= space) {
                return true;
            }
            
            uint8_t * msgBuf = (uint8_t *)msg->data();
            memcpy(buf + CORPC_MESSAGE_HEAD_SIZE, msgBuf, msgSize);
        } else {
            std::shared_ptr<google::protobuf::Message> msg = std::static_pointer_cast<google::protobuf::Message>(msgInfo->msg);
            
            msgSize = msg->GetCachedSize();
            if (msgSize == 0) {
                msgSize = msg->ByteSize();
            }
            
            if (msgSize + CORPC_MESSAGE_HEAD_SIZE >= space) {
                return true;
            }
            
            msg->SerializeWithCachedSizesToArray(buf + CORPC_MESSAGE_HEAD_SIZE);
        }
        
        *(uint32_t *)buf = htonl(msgSize);
        *(uint32_t *)(buf + 4) = htonl(msgInfo->type);
        size = CORPC_MESSAGE_HEAD_SIZE + msgSize;
        
        return true;
    }
    
    TcpMessageServer::TcpMessageServer( CoRpc::IO *io, const std::string& ip, uint16_t port): MessageServer(io) {
        _acceptor = new Acceptor(this, ip, port);
        
        std::vector<CoRpc::EncodeFunction> encodeFuns;
        encodeFuns.push_back(encode);
        
        _pipelineFactory = new TcpPipelineFactory(_worker, decode, std::move(encodeFuns), CORPC_MESSAGE_HEAD_SIZE, CORPC_MAX_MESSAGE_SIZE, 0, CoRpc::Pipeline::FOUR_BYTES);
    }
    
    TcpMessageServer::~TcpMessageServer() {}

    bool TcpMessageServer::start() {
        if (!MessageServer::start()) {
            return false;
        }
        
        // 启动acceptor
        if (!_acceptor->start()) {
            printf("ERROR: TcpMessageServer::start() -- start acceptor failed.\n");
            return false;
        }
        
        return true;
    }
    
}
