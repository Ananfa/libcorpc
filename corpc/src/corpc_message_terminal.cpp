/*
 * Created by Xianke Liu on 2024/6/29.
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

#include "corpc_message_terminal.h"
#include "corpc_routine_env.h"
#include "corpc_crc.h"

using namespace corpc;

MessageTerminal::Connection::Connection(int fd, IO *io, Worker *worker, MessageTerminal *terminal): corpc::Connection(fd, io, terminal->needHB_), terminal_(terminal), worker_(worker), crypter_(nullptr), recvSerial_(0), lastSendSerial_(0) {

}

MessageTerminal::Connection::~Connection() {
    DEBUG_LOG("MessageTerminal::Connection::~Connection -- fd:%d\n", fd_);
}

void MessageTerminal::Connection::onConnect() {
    MessageWorkerTask *task = MessageWorkerTask::create();
    task->type = CORPC_MSG_TYPE_CONNECT;
    task->banned = false;
    task->connection = std::static_pointer_cast<Connection>(shared_from_this());
    
    worker_->addTask(task);
}

void MessageTerminal::Connection::onClose() {
    MessageWorkerTask *task = MessageWorkerTask::create();
    task->type = CORPC_MSG_TYPE_CLOSE;
    task->banned = false;
    task->connection = std::static_pointer_cast<Connection>(shared_from_this());
    
    worker_->addTask(task);
}

void MessageTerminal::Connection::scrapMessages(uint32_t serial) {
    if (msgBuffer_) {
        msgBuffer_->scrapMessages(serial);
    }
}

void MessageTerminal::Connection::send(int32_t type, bool isRaw, bool needCrypt, bool needBuffer, uint16_t tag, std::shared_ptr<void> msg) {
    if (!isOpen()) {
        if (type < 0) {
            return;
        }

        if (!msgBuffer_) {
            return;
        }

        assert(terminal_->enableSerial_);

        if (!needBuffer) {
            return;
        }
    }

    // 注意：服务器向客户端发的序号为0的消息不进行序号校验，一般用于底层控制型消息（如：连接、心跳等，此类消息type<0）
    //      序号和发送缓存概念有点绑定，序号是用于消息校验，缓存用于重连补发消息。如果带序号的消息没进行缓存，补发消息时会因序号不连续导致校验不通过。
    //      因此，当配置了缓存时，不进行缓存的消息序号一定为0，进行缓存的消息序号一定不为0。
    //      由于缓存消息一定有序号，所以当配置了缓存时，terminal_->enableSerial_一定为true。
    // 问题：
    //      1.为了让上层功能的消息也可不进行缓存，是否可以将消息序号改为非强制，序号为0时不进行序号校验，大于0时才进行序号校验？不安全，无法防消息复制重发
    //      2.如何让一些上层消息不缓存但又有序号？重连补发消息时，当相邻两缓存消息序号不一致时补发一个特殊控制型消息（通知对方丢弃的消息序号范围）（TODO）

    std::shared_ptr<SendMessageInfo> sendInfo(new SendMessageInfo);
    sendInfo->type = type;
    sendInfo->isRaw = isRaw;
    sendInfo->needCrypt = needCrypt;
    sendInfo->tag = tag;
    sendInfo->msg = msg;

    if (type > 0 && terminal_->enableSerial_) {
        sendInfo->serial = ++lastSendSerial_;

        if (msgBuffer_) {
            if (needBuffer) {
                msgBuffer_->insertMessage(sendInfo);
            } else {
                msgBuffer_->jumpToSerial(sendInfo->serial);
            }
        }
    } else {
        sendInfo->serial = 0;
    }
    
    if (isOpen()) {
        corpc::Connection::send(sendInfo);
    }
}

void MessageTerminal::Connection::resend() {
    if (msgBuffer_) {
        uint32_t lastSerial = 0;
        msgBuffer_->traverse([this, &lastSerial](std::shared_ptr<SendMessageInfo> &sendInfo) -> bool {
            if (!isOpen()) {
                return false;
            }

            if (lastSerial > 0 && sendInfo->serial != lastSerial + 1) {
                // 发送序号跳过消息
                std::shared_ptr<SendMessageInfo> sendInfo1(new SendMessageInfo);
                sendInfo1->type = CORPC_MSG_TYPE_JUMP_SERIAL;
                sendInfo1->isRaw = true;
                sendInfo1->needCrypt = false;
                sendInfo1->tag = 0;
                sendInfo1->serial = sendInfo->serial - 1;

                corpc::Connection::send(sendInfo1);
            }

            corpc::Connection::send(sendInfo);
            lastSerial = sendInfo->serial;
            return true;
        });
    }
}

void * MessageTerminal::MessageWorkerTask::taskCallRoutine( void * arg ) {
    MessageWorkerTask *task = (MessageWorkerTask *)arg;
    
    MessageTerminal *terminal = task->connection->getTerminal();
    
    auto iter = terminal->registerMessageMap_.find(task->type);
    
    std::shared_ptr<google::protobuf::Message> msg = std::static_pointer_cast<google::protobuf::Message>(task->msg);
    iter->second.handle(task->type, task->tag, msg, task->connection);
    
    task->destory();
    
    return NULL;
}

void MessageTerminal::MessageWorkerTask::doTask() {
    // 注意：处理完消息需要自己删除msg
	MessageTerminal *terminal = connection->getTerminal();

    switch (type) {
        case CORPC_MSG_TYPE_CONNECT: // 新连接建立
            DEBUG_LOG("MessageTerminal::Worker::handleMessage -- fd %d connect\n", connection->getfd());
            // TODO:
            break;
        case CORPC_MSG_TYPE_CLOSE: // 连接断开
            DEBUG_LOG("MessageTerminal::Worker::handleMessage -- fd %d close\n", connection->getfd());
            // TODO:
            break;
        default:
            if (terminal->enableSerial_) {
                if (connection->msgBuffer_) {
                    connection->msgBuffer_->scrapMessages(reqSerial);
                }
            }

            if (type == CORPC_MSG_TYPE_HEARTBEAT) {
                destory();
                return;
            }
    }

    //DEBUG_LOG("MessageTerminal::Worker::handleMessage msgType:%d\n", type);

    auto iter = banned?terminal->registerMessageMap_.find(CORPC_MSG_TYPE_BANNED):terminal->registerMessageMap_.find(type);

    if (iter == terminal->registerMessageMap_.end()) {
        if (!banned && terminal->otherMessageHandle_) {
            // 其他消息处理（一般用于直接转发给其他服务器）
            std::shared_ptr<std::string> real_msg = std::static_pointer_cast<std::string>(msg);
            terminal->otherMessageHandle_(type, tag, real_msg, connection);
        } else {
            ERROR_LOG("MessageTerminal::Worker::handleMessage -- no handler with msg type %d\n", type);
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

MessageTerminal::MessageTerminal(bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial): needHB_(needHB), enableSendCRC_(enableSendCRC), enableRecvCRC_(enableRecvCRC), enableSerial_(enableSerial) {

}

MessageTerminal::~MessageTerminal() {
    for (auto &kv : registerMessageMap_) {
        if (kv.second.proto != NULL) {
            delete kv.second.proto;
        }
    }
}

bool MessageTerminal::registerMessage(int32_t type,
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

bool MessageTerminal::setBanMessages(std::list<int32_t> &msgTypes) {
    std::map<int32_t, bool> msgTypeM;
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

corpc::Connection *MessageTerminal::buildConnection(int fd, IO *io, Worker *worker) {
    return new Connection(fd, io, worker, this);
}

WorkerTask* MessageTerminal::decode(std::shared_ptr<corpc::Connection> &connection, uint8_t *head, uint8_t *body, int size) {
    std::shared_ptr<Connection> conn = std::static_pointer_cast<Connection>(connection);
    Crypter *crypter = conn->getCrypter();
    MessageTerminal *terminal = conn->getTerminal();
    
    int32_t msgType = *(int32_t *)(head + 4);
    msgType = be32toh(msgType);
    uint16_t tag = *(uint16_t *)(head + 8);
    tag = be16toh(tag);
    uint16_t flag = *(uint16_t *)(head + 10);
    flag = be16toh(flag);
    
    uint32_t reqSerial = 0;
    if (msgType < 0) {
        // 处理系统类型消息，如：心跳
        // 注意：如果是UDP握手消息怎么办？
        if (msgType == CORPC_MSG_TYPE_HEARTBEAT) {
            uint64_t nowms = mtime();
            connection->setLastRecvHBTime(nowms);

            // 接收最大序列号并发给worker处理消息清理
            if (terminal->enableSerial_) {
                reqSerial = *(uint32_t *)(head + 12);
                reqSerial = be32toh(reqSerial);

                MessageWorkerTask *task = MessageWorkerTask::create();
                task->type = msgType;
                task->reqSerial = reqSerial;
                task->connection = conn;
                
                return task;
            }
        } else if (msgType == CORPC_MSG_TYPE_JUMP_SERIAL) {
            uint32_t serial = *(uint32_t *)(head + 16);
            serial = be32toh(serial);

            if (serial <= conn->recvSerial_) {
                ERROR_LOG("MessageTerminal::decode -- jump serial invaild, %d <= %d\n", serial, conn->recvSerial_);
                connection->setDecodeError();
                return nullptr;
            }

            conn->recvSerial_ = serial;
        } else {
            WARN_LOG("MessageTerminal::decode -- recv system message: %d\n", msgType);
        }
        
        return nullptr;
    }

    // 注意：控制类消息（如：心跳、握手）不进行序号判断、CRC校验以及加解密处理
    // 序号判断
    if (terminal->enableSerial_) {
        conn->recvSerial_++;

        uint32_t serial = *(uint32_t *)(head + 16);
        serial = be32toh(serial);

        if (conn->recvSerial_ != serial) {
            ERROR_LOG("MessageTerminal::decode -- serial not match, need:%d get:%d\n", conn->recvSerial_, serial);
            connection->setDecodeError();
            return nullptr;
        }

        reqSerial = *(uint32_t *)(head + 12);
        reqSerial = be32toh(reqSerial);
    }

    // CRC校验（CRC码计算需要包含除crc外的包头）
    if (terminal->enableRecvCRC_) {
        uint16_t crc = *(uint16_t *)(head + 20);
        crc = be16toh(crc);

        //uint16_t crc1 = CRC::CheckSum(head, 0xFFFF, 18);
        //crc1 = CRC::CheckSum(body, crc1, size);
        uint16_t crc1 = CRC::CheckSum(body, 0xFFFF, size);

        if (crc != crc1) {
            ERROR_LOG("MessageTerminal::decode -- crc not match, need:%d get:%d\n", crc1, crc);
            connection->setDecodeError();
            return nullptr;
        }
    }

    // 解密
    if ((flag & CORPC_MESSAGE_FLAG_CRYPT) != 0) {
        if (crypter == nullptr) {
            ERROR_LOG("MessageTerminal::decode -- decrypt fail for no crypter, msgType:%d, fd: %d\n", msgType, conn->getfd());
            connection->setDecodeError();
            return nullptr;
        }

        crypter->decrypt(body, body, size);
    }

    auto iter = terminal->registerMessageMap_.find(msgType);
    if (iter == terminal->registerMessageMap_.end()) {
        if (terminal->otherMessageHandle_) {
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
            ERROR_LOG("MessageTerminal::decode -- unknown message: %d\n", msgType);
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
                    ERROR_LOG("MessageTerminal::decode -- parse body fail for message: %d\n", msgType);
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

bool MessageTerminal::encode(std::shared_ptr<corpc::Connection> &connection, std::shared_ptr<void>& data, uint8_t *buf, int space, int &size, std::string &downflowBuf, uint32_t &downflowBufSentNum) {
    std::shared_ptr<SendMessageInfo> msgInfo = std::static_pointer_cast<SendMessageInfo>(data);
    std::shared_ptr<Connection> conn = std::static_pointer_cast<Connection>(connection);

    // 若空间不足容纳消息头部则等待下次
    if (CORPC_MESSAGE_HEAD_SIZE > space) {
        return true;
    }

    bool needCrypt = msgInfo->needCrypt;
    Crypter *crypter = conn->getCrypter();
    if (needCrypt && crypter == nullptr) {
        return false;
    }

    bool needCRC = conn->terminal_->enableSendCRC_;
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
    *(uint32_t *)(buf + 4) = htobe32(msgInfo->type);
    
    // 头部设置加密标志
    *(uint16_t *)(buf + 8) = htobe16(msgInfo->tag);
    uint16_t flag = needCrypt?CORPC_MESSAGE_FLAG_CRYPT:0;
    *(uint16_t *)(buf + 10) = htobe16(flag);
    *(uint32_t *)(buf + 12) = htobe32(conn->recvSerial_);
    *(uint32_t *)(buf + 16) = htobe32(msgInfo->serial);
    *(uint16_t *)(buf + 20) = htobe16(crc);

    return true;
}
