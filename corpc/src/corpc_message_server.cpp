//
//  corpc_message_server.cpp
//  corpc
//
//  Created by Xianke Liu on 2018/4/23.
//Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_routine_env.h"
#include "corpc_message_server.h"

#include <sys/time.h>

namespace corpc {
    
    MessageServer::Connection::Connection(int fd, MessageServer* server): corpc::Connection(fd, server->_io, server->_needHB), _server(server) {
        struct timeval now = { 0 };
        gettimeofday( &now,NULL );
        _createTime = now.tv_sec;
    }
    
    MessageServer::Connection::~Connection() {
        LOG("MessageServer::Connection::~Connection -- fd:%d\n", _fd);
    }
    
    void MessageServer::Connection::onClose() {
        std::shared_ptr<corpc::Connection> self = corpc::Connection::shared_from_this();
        _server->onClose(self);
    }
    
    void MessageServer::Connection::send(int32_t type, bool isRaw, std::shared_ptr<void> msg) {
        std::shared_ptr<corpc::SendMessageInfo> sendInfo(new corpc::SendMessageInfo);
        sendInfo->type = type;
        sendInfo->isRaw = false;
        sendInfo->msg = msg;
        
        corpc::Connection::send(sendInfo);
    }
    
    void * MessageServer::Worker::taskCallRoutine( void * arg ) {
        WorkerTask *task = (WorkerTask *)arg;
        
        MessageServer *server = static_cast<MessageServer *>(task->connection->getServer());
        
        auto iter = server->_registerMessageMap.find(task->type);
        
        iter->second.handle(task->msg, task->connection);
        
        delete task;
        
        return NULL;
    }
    
    void MessageServer::Worker::handleMessage(void *msg) {
        // 注意：处理完消息需要自己删除msg
        WorkerTask *task = (WorkerTask *)msg;
        
        switch (task->type) {
            case CORPC_MSG_TYPE_CONNECT: // 新连接建立
                LOG("MessageServer::Worker::handleMessage -- fd %d connect\n", task->connection->getfd());
                // TODO:
                break;
            case CORPC_MSG_TYPE_CLOSE: // 连接断开
                LOG("MessageServer::Worker::handleMessage -- fd %d close\n", task->connection->getfd());
                // TODO:
                break;
        }
        
        auto iter = _server->_registerMessageMap.find(task->type);
        if (iter == _server->_registerMessageMap.end()) {
            ERROR_LOG("MessageServer::Worker::handleMessage -- no handler with msg type %d\n", task->type);
            delete task;
            return;
        }
        
        if (iter->second.needCoroutine) {
            corpc::RoutineEnvironment::startCoroutine(taskCallRoutine, task);
        } else {
            iter->second.handle(task->msg, task->connection);
            
            delete task;
        }
    }
    
    MessageServer::MessageServer(IO *io, bool needHB): corpc::Server(io), _needHB(needHB) {
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
        info.needCoroutine = needCoroutine;
        info.handle = handle;
        
        _registerMessageMap.insert(std::make_pair(type, info));
        
        return false;
    }
    
    corpc::Connection *MessageServer::buildConnection(int fd) {
        return new Connection(fd, this);
    }
    
    void MessageServer::onConnect(std::shared_ptr<corpc::Connection>& connection) {
        WorkerTask *task = new WorkerTask;
        task->type = CORPC_MSG_TYPE_CONNECT;
        task->connection = std::static_pointer_cast<Connection>(connection);
        
        _worker->addMessage(task);
    }
    
    void MessageServer::onClose(std::shared_ptr<corpc::Connection>& connection) {
        WorkerTask *task = new WorkerTask;
        task->type = CORPC_MSG_TYPE_CLOSE;
        task->connection = std::static_pointer_cast<Connection>(connection);
        
        _worker->addMessage(task);
    }
    
    void* MessageServer::decode(std::shared_ptr<corpc::Connection> &connection, uint8_t *head, uint8_t *body, int size) {
        std::shared_ptr<Connection> conn = std::static_pointer_cast<Connection>(connection);
        
        MessageServer *server = conn->getServer();
        
        uint32_t bodySize = *(uint32_t *)head;
        bodySize = be32toh(bodySize);
        int32_t msgType = *(int32_t *)(head + 4);
        msgType = be32toh(msgType);
        
        // 处理系统类型消息，如：心跳
        // 注意：如果是UDP握手消息怎么办？
        if (msgType < 0) {
            if (msgType == CORPC_MSG_TYPE_HEARTBEAT) {
                struct timeval now = { 0 };
                gettimeofday( &now,NULL );
                uint64_t nowms = now.tv_sec;
                nowms *= 1000;
                nowms += now.tv_usec / 1000;
                
                connection->setLastRecvHBTime(nowms);
            } else {
                WARN_LOG("MessageServer::decode -- recv system message: %d\n", msgType);
            }
            
            return nullptr;
        }
        
        auto iter = server->_registerMessageMap.find(msgType);
        if (iter == server->_registerMessageMap.end()) {
            ERROR_LOG("MessageServer::decode -- unknown message: %d\n", msgType);
            connection->setDecodeError();
            return nullptr;
        }
        
        google::protobuf::Message *msg = iter->second.proto->New();
        if (!msg->ParseFromArray(body, size)) {
            // 出错处理
            ERROR_LOG("MessageServer::decode -- parse body fail for message: %d\n", msgType);
            connection->setDecodeError();
            return nullptr;
        }
        
        WorkerTask *task = new WorkerTask;
        task->type = msgType;
        task->connection = conn;
        task->msg = std::shared_ptr<google::protobuf::Message>(msg);
        
        return task;
    }
    
    bool MessageServer::encode(std::shared_ptr<corpc::Connection> &connection, std::shared_ptr<void>& data, uint8_t *buf, int space, int &size, std::string &downflowBuf, uint32_t &downflowBufSentNum) {
        std::shared_ptr<SendMessageInfo> msgInfo = std::static_pointer_cast<SendMessageInfo>(data);
        
        // 若空间不足容纳消息头部则等待下次
        if (CORPC_MESSAGE_HEAD_SIZE > space) {
            return true;
        }
        
        uint32_t msgSize;
        if (msgInfo->isRaw) {
            std::shared_ptr<std::string> msg = std::static_pointer_cast<std::string>(msgInfo->msg);
            
            msgSize = msg?msg->size():0;
            
            int spaceleft = space - CORPC_MESSAGE_HEAD_SIZE;
            if (spaceleft >= msgSize) {
                if (msgSize > 0) {
                    uint8_t * msgBuf = (uint8_t *)msg->data();
                    memcpy(buf + CORPC_MESSAGE_HEAD_SIZE, msgBuf, msgSize);
                }
                
                size = CORPC_MESSAGE_HEAD_SIZE + msgSize;
            } else {
                downflowBuf = *msg;
                uint8_t *dbuf = (uint8_t*)downflowBuf.data();
                
                if (spaceleft > 0) {
                    memcpy(buf + CORPC_MESSAGE_HEAD_SIZE, dbuf, spaceleft);
                    downflowBufSentNum = spaceleft;
                }
                
                size = space;
            }
        } else {
            std::shared_ptr<google::protobuf::Message> msg = std::static_pointer_cast<google::protobuf::Message>(msgInfo->msg);
            
            msgSize = msg->GetCachedSize();
            if (msgSize == 0) {
                msgSize = msg->ByteSize();
            }
            
            int spaceleft = space - CORPC_MESSAGE_HEAD_SIZE;
            if (spaceleft >= msgSize) {
                if (msgSize > 0) {
                    msg->SerializeWithCachedSizesToArray(buf + CORPC_MESSAGE_HEAD_SIZE);
                }
                
                size = CORPC_MESSAGE_HEAD_SIZE + msgSize;
            } else {
                downflowBuf.assign(msgSize, 0);
                uint8_t *dbuf = (uint8_t*)downflowBuf.data();
                msg->SerializeWithCachedSizesToArray(dbuf);
                
                if (spaceleft > 0) {
                    memcpy(buf + CORPC_MESSAGE_HEAD_SIZE, dbuf, spaceleft);
                    downflowBufSentNum = spaceleft;
                }
                
                size = space;
            }
        }
        
        *(uint32_t *)buf = htobe32(msgSize);
        *(uint32_t *)(buf + 4) = htobe32(msgInfo->type);
        
        return true;
    }
    
    TcpMessageServer::TcpMessageServer(corpc::IO *io, bool needHB, const std::string& ip, uint16_t port): MessageServer(io, needHB) {
        _acceptor = new TcpAcceptor(this, ip, port);
        
        _pipelineFactory = new TcpPipelineFactory(_worker, decode, encode, CORPC_MESSAGE_HEAD_SIZE, CORPC_MAX_MESSAGE_SIZE, 0, corpc::MessagePipeline::FOUR_BYTES);
    }
    
    UdpMessageServer::UdpMessageServer(corpc::IO *io, bool needHB, const std::string& ip, uint16_t port): MessageServer(io, needHB) {
        _acceptor = new UdpAcceptor(this, ip, port);
        
        _pipelineFactory = new UdpPipelineFactory(_worker, decode, encode, CORPC_MESSAGE_HEAD_SIZE, CORPC_MAX_UDP_MESSAGE_SIZE);
    }
    
}
