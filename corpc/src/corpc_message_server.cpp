/*
 * Created by Xianke Liu on 2018/4/3.
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
#include "corpc_message_server.h"
#include "corpc_crc.h"

#include <sys/time.h>

using namespace corpc;

MessageServer::Connection::Connection(int fd, MessageServer* server): corpc::Connection(fd, server->io_, server->needHB_), server_(server), crypter_(nullptr), recvSerial_(0) {
    time(&createTime_);
}

MessageServer::Connection::~Connection() {
    DEBUG_LOG("MessageServer::Connection::~Connection -- fd:%d\n", fd_);
}

void MessageServer::Connection::onClose() {
    std::shared_ptr<corpc::Connection> self = corpc::Connection::shared_from_this();
    server_->onClose(self);
}

void MessageServer::Connection::scrapMessages(uint32_t serial) {
    if (msgBuffer_) {
        msgBuffer_->scrapMessages(serial);
    }
}

void MessageServer::Connection::send(int16_t type, bool isRaw, bool needCrypt, bool needBuffer, uint16_t tag, std::shared_ptr<void> msg) {
    if (!isOpen() && !(server_->enableSerial_ && msgBuffer_->needBuf())) {
        return;
    }

    std::shared_ptr<SendMessageInfo> sendInfo(new SendMessageInfo);
    sendInfo->type = type;
    sendInfo->isRaw = isRaw;
    sendInfo->needCrypt = needCrypt;
    sendInfo->tag = tag;
    sendInfo->msg = msg;

    if (needBuffer && server_->enableSerial_) {
        assert(msgBuffer_);
        msgBuffer_->insertMessage(sendInfo);
    } else {
        // 注意：服务器向客户端发的序号为0的消息不进行序号校验，用于给上层发特殊消息
        sendInfo->serial = 0;
    }
    
    if (isOpen()) {
        corpc::Connection::send(sendInfo);
    }
}

void MessageServer::Connection::resend() {
    if (msgBuffer_) {
        msgBuffer_->traverse([this](std::shared_ptr<SendMessageInfo> &sendInfo) -> bool {
            if (!isOpen()) {
                return false;
            }

            corpc::Connection::send(sendInfo);
            return true;
        });
    }
}

/*
void * MessageServer::Worker::taskCallRoutine( void * arg ) {
    MessageWorkerTask *task = (MessageWorkerTask *)arg;
    
    MessageServer *server = static_cast<MessageServer *>(task->connection->getServer());
    
    auto iter = server->registerMessageMap_.find(task->type);
    
    std::shared_ptr<google::protobuf::Message> msg = std::static_pointer_cast<google::protobuf::Message>(task->msg);
    iter->second.handle(task->type, task->tag, msg, task->connection);
    
    delete task;
    
    return NULL;
}

void MessageServer::Worker::handleTask(WorkerTask *wt) {
    // 注意：处理完消息需要自己删除msg
    MessageWorkerTask *task = (MessageWorkerTask *)wt;

    switch (task->type) {
        case CORPC_MSG_TYPE_CONNECT: // 新连接建立
            DEBUG_LOG("MessageServer::Worker::handleMessage -- fd %d connect\n", task->connection->getfd());
            // TODO:
            break;
        case CORPC_MSG_TYPE_CLOSE: // 连接断开
            DEBUG_LOG("MessageServer::Worker::handleMessage -- fd %d close\n", task->connection->getfd());
            // TODO:
            break;
        default:
            if (task->connection->getServer()->enableSerial_) {
                if (task->connection->msgBuffer_) {
                    task->connection->msgBuffer_->scrapMessages(task->reqSerial);
                }
            }

            if (task->type == CORPC_MSG_TYPE_HEARTBEAT) {
                delete task;
                return;
            }
    }

    //DEBUG_LOG("MessageServer::Worker::handleMessage msgType:%d\n", task->type);

    auto iter = task->banned?server_->registerMessageMap_.find(CORPC_MSG_TYPE_BANNED):server_->registerMessageMap_.find(task->type);

    if (iter == server_->registerMessageMap_.end()) {
        if (!task->banned && server_->otherMessageHandle_) {
            // 其他消息处理（一般用于直接转发给其他服务器）
            std::shared_ptr<std::string> msg = std::static_pointer_cast<std::string>(task->msg);
            server_->otherMessageHandle_(task->type, task->tag, msg, task->connection);
        } else {
            ERROR_LOG("MessageServer::Worker::handleMessage -- no handler with msg type %d\n", task->type);
        }

        delete task;
        return;
    }

    if (iter->second.needCoroutine) {
        corpc::RoutineEnvironment::startCoroutine(taskCallRoutine, task);
    } else {
        std::shared_ptr<google::protobuf::Message> msg = std::static_pointer_cast<google::protobuf::Message>(task->msg);
        iter->second.handle(task->type, task->tag, msg, task->connection);
        
        delete task;
    }
}
*/

void * MessageServer::MessageWorkerTask::taskCallRoutine( void * arg ) {
    MessageWorkerTask *task = (MessageWorkerTask *)arg;
    
    MessageServer *server = static_cast<MessageServer *>(task->connection->getServer());
    
    auto iter = server->registerMessageMap_.find(task->type);
    
    std::shared_ptr<google::protobuf::Message> msg = std::static_pointer_cast<google::protobuf::Message>(task->msg);
    iter->second.handle(task->type, task->tag, msg, task->connection);
    
    task->destory();
    
    return NULL;
}

void MessageServer::MessageWorkerTask::doTask() {
    // 注意：处理完消息需要自己删除msg
    switch (type) {
        case CORPC_MSG_TYPE_CONNECT: // 新连接建立
            DEBUG_LOG("MessageServer::Worker::handleMessage -- fd %d connect\n", connection->getfd());
            // TODO:
            break;
        case CORPC_MSG_TYPE_CLOSE: // 连接断开
            DEBUG_LOG("MessageServer::Worker::handleMessage -- fd %d close\n", connection->getfd());
            // TODO:
            break;
        default:
            if (connection->getServer()->enableSerial_) {
                if (connection->msgBuffer_) {
                    connection->msgBuffer_->scrapMessages(reqSerial);
                }
            }

            if (type == CORPC_MSG_TYPE_HEARTBEAT) {
                destory();
                return;
            }
    }

    //DEBUG_LOG("MessageServer::Worker::handleMessage msgType:%d\n", type);

    auto iter = banned?connection->getServer()->registerMessageMap_.find(CORPC_MSG_TYPE_BANNED):connection->getServer()->registerMessageMap_.find(type);

    if (iter == connection->getServer()->registerMessageMap_.end()) {
        if (!banned && connection->getServer()->otherMessageHandle_) {
            // 其他消息处理（一般用于直接转发给其他服务器）
            std::shared_ptr<std::string> real_msg = std::static_pointer_cast<std::string>(msg);
            connection->getServer()->otherMessageHandle_(type, tag, real_msg, connection);
        } else {
            ERROR_LOG("MessageServer::Worker::handleMessage -- no handler with msg type %d\n", type);
        }

        destory();
        return;
    }

    if (iter->second.needCoroutine) {
        RoutineEnvironment::startCoroutine(taskCallRoutine, this);
    } else {
        std::shared_ptr<google::protobuf::Message> real_msg = std::static_pointer_cast<google::protobuf::Message>(msg);
        iter->second.handle(type, tag, real_msg, connection);
        
        destory();
    }
}

MessageServer::MessageServer(IO *io, Worker *worker, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial): corpc::Server(io), needHB_(needHB), enableSendCRC_(enableSendCRC), enableRecvCRC_(enableRecvCRC), enableSerial_(enableSerial) {
    if (worker != NULL) {
        worker_ = worker;
    } else {
        worker_ = io->getWorker();
    }
}

MessageServer::~MessageServer() {}

bool MessageServer::registerMessage(int type,
                                    google::protobuf::Message *proto,
                                    bool needCoroutine,
                                    MessageHandle handle) {
    if (registerMessageMap_.find(type) != registerMessageMap_.end()) {
        return false;
    }


    RegisterMessageInfo info;
    //info.type = type;
    info.proto = proto;
    info.needCoroutine = needCoroutine;
    info.banned = false;
    info.handle = handle;
    
    registerMessageMap_.insert(std::make_pair(type, info));
    
    return true;
}

bool MessageServer::setBanMessages(std::list<int> &msgTypes) {
    std::map<int, bool> msgTypeM;
    for (auto msgType : msgTypes) {
        msgTypeM[msgType] = true;
    }

    for (auto &kv : registerMessageMap_) {
        if (msgTypeM.find(kv.first) != msgTypeM.end()) {
            if (!kv.second.banned) {
                kv.second.banned = true;
            }
        } else {
            if (kv.second.banned) {
                kv.second.banned = false;
            }
        }
    }
}

//bool MessageServer::banMessage(int type) {
//    if (registerMessageMap_.find(type) == registerMessageMap_.end()) {
//        return false;
//    }
//
//    registerMessageMap_[type].banned = true;
//    return true;
//}
//
//bool MessageServer::unbanMessage(int type) {
//    if (registerMessageMap_.find(type) == registerMessageMap_.end()) {
//        return false;
//    }
//
//    registerMessageMap_[type].banned = false;
//    return true;
//}

corpc::Connection *MessageServer::buildConnection(int fd) {
    return new Connection(fd, this);
}

void MessageServer::onConnect(std::shared_ptr<corpc::Connection>& connection) {
    MessageWorkerTask *task = MessageWorkerTask::create();
    task->type = CORPC_MSG_TYPE_CONNECT;
    task->banned = false;
    task->connection = std::static_pointer_cast<Connection>(connection);
    
    worker_->addTask(task);
}

void MessageServer::onClose(std::shared_ptr<corpc::Connection>& connection) {
    MessageWorkerTask *task = MessageWorkerTask::create();
    task->type = CORPC_MSG_TYPE_CLOSE;
    task->banned = false;
    task->connection = std::static_pointer_cast<Connection>(connection);
    
    worker_->addTask(task);
}

WorkerTask* MessageServer::decode(std::shared_ptr<corpc::Connection> &connection, uint8_t *head, uint8_t *body, int size) {
    std::shared_ptr<Connection> conn = std::static_pointer_cast<Connection>(connection);
    std::shared_ptr<Crypter> crypter = conn->getCrypter();
    MessageServer *server = conn->getServer();
    
    int16_t msgType = *(int16_t *)(head + 4);
    msgType = be16toh(msgType);
    uint16_t tag = *(uint16_t *)(head + 6);
    tag = be16toh(tag);
    uint16_t flag = *(uint16_t *)(head + 8);
    flag = be16toh(flag);
    
    uint32_t reqSerial = 0;
    if (msgType < 0) {
        // 处理系统类型消息，如：心跳
        // 注意：如果是UDP握手消息怎么办？
        if (msgType == CORPC_MSG_TYPE_HEARTBEAT) {
            uint64_t nowms = mtime();
            connection->setLastRecvHBTime(nowms);

            // 接收最大序列号并发给worker处理消息清理
            if (server->enableSerial_) {
                reqSerial = *(uint32_t *)(head + 10);
                reqSerial = be32toh(reqSerial);

                MessageWorkerTask *task = MessageWorkerTask::create();
                task->type = msgType;
                task->reqSerial = reqSerial;
                task->connection = conn;
                
                return task;
            }
        } else {
            WARN_LOG("MessageServer::decode -- recv system message: %d\n", msgType);
        }
        
        return nullptr;
    }

    // 注意：控制类消息（如：心跳、握手）不进行序号判断、CRC校验以及加解密处理
    // 序号判断
    if (server->enableSerial_) {
        conn->recvSerial_++;

        uint32_t serial = *(uint32_t *)(head + 14);
        serial = be32toh(serial);

        if (conn->recvSerial_ != serial) {
            ERROR_LOG("MessageServer::decode -- serial not match, need:%d get:%d\n", conn->recvSerial_, serial);
            connection->setDecodeError();
            return nullptr;
        }

        reqSerial = *(uint32_t *)(head + 10);
        reqSerial = be32toh(reqSerial);
    }

    // CRC校验（CRC码计算需要包含除crc外的包头）
    if (server->enableRecvCRC_) {
        uint16_t crc = *(uint16_t *)(head + 18);
        crc = be16toh(crc);

        uint16_t crc1 = CRC::CheckSum(head, 0xFFFF, 18);
        crc1 = CRC::CheckSum(body, crc1, size);

        if (crc != crc1) {
            ERROR_LOG("MessageServer::decode -- crc not match, need:%d get:%d\n", crc1, crc);
            connection->setDecodeError();
            return nullptr;
        }
    }

    // 解密
    if ((flag & CORPC_MESSAGE_FLAG_CRYPT) != 0) {
        if (crypter == nullptr) {
            ERROR_LOG("MessageServer::decode -- decrypt fail for no crypter, msgType:%d, fd: %d\n", msgType, conn->getfd());
            connection->setDecodeError();
            return nullptr;
        }

        crypter->decrypt(body, body, size);
    }

    auto iter = server->registerMessageMap_.find(msgType);
    if (iter == server->registerMessageMap_.end()) {
        if (server->otherMessageHandle_) {
            // 旁路消息处理
            MessageWorkerTask *task = MessageWorkerTask::create();
            task->type = msgType;
            task->tag = tag;
            task->reqSerial = reqSerial;
            task->banned = false;
            task->connection = conn;
            task->msg = std::make_shared<std::string>((char *)body, size);
            
            return task;
        } else {
            ERROR_LOG("MessageServer::decode -- unknown message: %d\n", msgType);
            connection->setDecodeError();
            return nullptr;
        }
    } else {
        google::protobuf::Message *msg = NULL;

        // 由于iter->second.banned值会在其他线程修改，这里需要先记录下开始处理时的banned值，以免后续改变不一致
        bool banned = iter->second.banned; 
        
        if (!banned) {
            if (iter->second.proto) {
                msg = iter->second.proto->New();
                if (!msg->ParseFromArray(body, size)) {
                    // 出错处理
                    ERROR_LOG("MessageServer::decode -- parse body fail for message: %d\n", msgType);
                    connection->setDecodeError();
                    delete msg;
                    return nullptr;
                }
            } else {
                assert(size == 0);
            }
        }

        MessageWorkerTask *task = MessageWorkerTask::create();
        task->type = msgType;
        task->tag = tag;
        task->reqSerial = reqSerial;
        task->banned = banned;
        task->connection = conn;
        task->msg = std::shared_ptr<google::protobuf::Message>(msg);
        
        return task;
    }
    
}

bool MessageServer::encode(std::shared_ptr<corpc::Connection> &connection, std::shared_ptr<void>& data, uint8_t *buf, int space, int &size, std::string &downflowBuf, uint32_t &downflowBufSentNum) {
    std::shared_ptr<SendMessageInfo> msgInfo = std::static_pointer_cast<SendMessageInfo>(data);
    std::shared_ptr<Connection> conn = std::static_pointer_cast<Connection>(connection);

    // 若空间不足容纳消息头部则等待下次
    if (CORPC_MESSAGE_HEAD_SIZE > space) {
        return true;
    }

    bool needCrypt = msgInfo->needCrypt;
    std::shared_ptr<Crypter> crypter = conn->getCrypter();
    if (needCrypt && crypter == nullptr) {
        return false;
    }

    bool needCRC = conn->server_->enableSendCRC_;
    uint16_t crc = 0; // crc要求：服务器向客户端的CRC编码不包含序列号两字节，客户端向服务器编码需要包含序列号两字节
    uint32_t msgSize;
    if (msgInfo->isRaw) {
        std::shared_ptr<std::string> msg = std::static_pointer_cast<std::string>(msgInfo->msg);
        
        msgSize = msg?msg->size():0;

        int spaceleft = space - CORPC_MESSAGE_HEAD_SIZE;
        if (spaceleft >= msgSize) {
            if (msgSize > 0) {
                uint8_t * msgBuf = (uint8_t *)msg->data();
                memcpy(buf + CORPC_MESSAGE_HEAD_SIZE, msgBuf, msgSize);

                if (needCrypt) {
                    crypter->encrypt(buf + CORPC_MESSAGE_HEAD_SIZE, buf + CORPC_MESSAGE_HEAD_SIZE, msgSize);
                }

                if (needCRC) {
                    crc = CRC::CheckSum(buf + CORPC_MESSAGE_HEAD_SIZE, 0xFFFF, msgSize);
                }
            }
            
            size = CORPC_MESSAGE_HEAD_SIZE + msgSize;
        } else {
            downflowBuf = *msg;
            uint8_t* dbuf = (uint8_t*)downflowBuf.data();

            if (needCrypt) {
                crypter->encrypt(dbuf, dbuf, msgSize);
            }

            if (needCRC) {
                crc = CRC::CheckSum(dbuf, 0xFFFF, msgSize);
            }

            if (spaceleft > 0) {
                memcpy(buf + CORPC_MESSAGE_HEAD_SIZE, dbuf, spaceleft);
                downflowBufSentNum = spaceleft;
            } else {
                downflowBufSentNum = 0;
            }
            
            size = space;
        }
    } else {
        std::shared_ptr<google::protobuf::Message> msg = std::static_pointer_cast<google::protobuf::Message>(msgInfo->msg);
        
        msgSize = msg->GetCachedSize();
        if (msgSize == 0) {
            msgSize = msg->ByteSizeLong();
        }

        int spaceleft = space - CORPC_MESSAGE_HEAD_SIZE;
        if (spaceleft >= msgSize) {
            if (msgSize > 0) {
                msg->SerializeWithCachedSizesToArray(buf + CORPC_MESSAGE_HEAD_SIZE);

                if (needCrypt) {
                    crypter->encrypt(buf + CORPC_MESSAGE_HEAD_SIZE, buf + CORPC_MESSAGE_HEAD_SIZE, msgSize);
                }

                if (needCRC) {
                    crc = CRC::CheckSum(buf + CORPC_MESSAGE_HEAD_SIZE, 0xFFFF, msgSize);
                }
            }
            
            size = CORPC_MESSAGE_HEAD_SIZE + msgSize;
        } else {
            downflowBuf.assign(msgSize, 0);
            uint8_t *dbuf = (uint8_t*)downflowBuf.data();
            msg->SerializeWithCachedSizesToArray(dbuf);

            if (needCrypt) {
                crypter->encrypt(dbuf, dbuf, msgSize);
            }

            if (needCRC) {
                crc = CRC::CheckSum(dbuf, 0xFFFF, msgSize);
            }

            if (spaceleft > 0) {
                memcpy(buf + CORPC_MESSAGE_HEAD_SIZE, dbuf, spaceleft);
                downflowBufSentNum = spaceleft;
            } else {
                downflowBufSentNum = 0;
            }
            
            size = space;
        }
    }
    
    *(uint32_t *)buf = htobe32(msgSize);
    *(uint16_t *)(buf + 4) = htobe16(msgInfo->type);
    
    // 头部设置加密标志
    *(uint16_t *)(buf + 6) = htobe16(msgInfo->tag);
    uint16_t flag = needCrypt?CORPC_MESSAGE_FLAG_CRYPT:0;
    *(uint16_t *)(buf + 8) = htobe16(flag);
    *(uint32_t *)(buf + 14) = htobe32(msgInfo->serial);
    *(uint16_t *)(buf + 18) = htobe16(crc);

    return true;
}

TcpMessageServer::TcpMessageServer(IO *io, Worker *worker, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, const std::string& ip, uint16_t port): MessageServer(io, worker, needHB, enableSendCRC, enableRecvCRC, enableSerial) {
    acceptor_ = new TcpAcceptor(this, ip, port);
    
    pipelineFactory_.reset(new TcpPipelineFactory(worker_, decode, encode, CORPC_MESSAGE_HEAD_SIZE, CORPC_MAX_MESSAGE_SIZE, 0, MessagePipeline::FOUR_BYTES));
}

UdpMessageServer::UdpMessageServer(IO *io, Worker *worker, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, const std::string& ip, uint16_t port): MessageServer(io, worker, needHB, enableSendCRC, enableRecvCRC, enableSerial) {
    acceptor_ = new UdpAcceptor(this, ip, port);
    
    pipelineFactory_.reset(new UdpPipelineFactory(worker_, decode, encode, CORPC_MESSAGE_HEAD_SIZE, CORPC_MAX_UDP_MESSAGE_SIZE));
}
