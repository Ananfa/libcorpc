/*
 * Created by Xianke Liu on 2022/8/26.
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

#include "corpc_kcp.h"
#include "corpc_routine_env.h"

using namespace corpc;

KcpMessageTerminal::Connection::Connection(int fd, IO *io, Worker *worker, KcpMessageTerminal* terminal): MessageTerminal::Connection(fd, io, worker, terminal) {
DEBUG_LOG("KcpMessageTerminal::Connection::Connection -- fd:%d\n", fd_);
    pkcp_ = ikcp_create(0x1, (void *)this);
    ikcp_nodelay(pkcp_, 1, 20, 2, 1);
    pkcp_->output = rawOut;
}

KcpMessageTerminal::Connection::~Connection() {
    DEBUG_LOG("KcpMessageTerminal::Connection::~Connection -- fd:%d\n", fd_);
    ikcp_release(pkcp_);
}

ssize_t KcpMessageTerminal::Connection::write(const void *buf, size_t nbyte) {
    // 注意：这里分割数据，方便设定接收缓冲区大小
    int ret;
    uint32_t sentNum = 0;
    uint32_t leftNum = nbyte;

    do {
        uint32_t pkgSize = (leftNum > CORPC_MAX_KCP_PACKAGE_SIZE)?CORPC_MAX_KCP_PACKAGE_SIZE:leftNum;

        ret = kcpSend((const char *)(buf + sentNum), pkgSize);
        if (ret < 0) {
            WARN_LOG("KcpMessageTerminal::Connection::write -- kcpSend ret %d\n", ret);

            return ret;
        }

        sentNum += pkgSize;
        leftNum -= pkgSize;
    } while(leftNum > 0);

    kcpFlush();

    return nbyte;
}

void KcpMessageTerminal::Connection::onSenderInit() {
    RoutineEnvironment::startCoroutine(updateRoutine, this);
}

void KcpMessageTerminal::Connection::kcpUpdate(uint32_t current) {
    LockGuard lock(kcpMtx_);
    ikcp_update(pkcp_, current);
}

uint32_t KcpMessageTerminal::Connection::kcpCheck(uint32_t current) {
    LockGuard lock(kcpMtx_);
    return ikcp_check(pkcp_, current);
}

int KcpMessageTerminal::Connection::kcpInput(const char *data, long size) {
    LockGuard lock(kcpMtx_);
    return ikcp_input(pkcp_, data, size);
}

int KcpMessageTerminal::Connection::kcpSend(const char *buffer, int len) {
    LockGuard lock(kcpMtx_);
    return ikcp_send(pkcp_, buffer, len);
}

int KcpMessageTerminal::Connection::kcpRecv(char *buffer, int len) {
    LockGuard lock(kcpMtx_);
    return ikcp_recv(pkcp_, buffer, len);
}

void KcpMessageTerminal::Connection::kcpFlush() {
    LockGuard lock(kcpMtx_);
    ikcp_flush(pkcp_);
}

void * KcpMessageTerminal::Connection::updateRoutine( void * arg ) {
    std::shared_ptr<KcpMessageTerminal::Connection> connection = std::static_pointer_cast<KcpMessageTerminal::Connection>(((KcpMessageTerminal::Connection*)arg)->getPtr());

    while (true) {
        if (connection->isClosing_) {
            break;
        }

        uint64_t now = mtime();
        uint32_t current = (uint32_t)(now & 0xfffffffful);
        connection->kcpUpdate(current);

        uint32_t next = connection->kcpCheck(current);
        if (next > current) {
            // FixMe：这里会导致要发送的消息得不到立即发送
            //msleep(next-current);
            msleep(1);
        }
    }

    DEBUG_LOG("KcpMessageTerminal::Connection::updateRoutine ended, fd:%d\n", connection->fd_);
    return NULL;
}

int KcpMessageTerminal::Connection::rawOut(const char *buf, int len, ikcpcb *kcp, void *obj) {
    KcpMessageTerminal::Connection* conn = (KcpMessageTerminal::Connection*)obj;
    int ret;
    uint32_t sentNum = 0;
    uint32_t leftNum = len;

    do {
        ret = (int)::write(conn->fd_, buf + sentNum, leftNum);
        if (ret > 0) {
            assert(ret <= leftNum);
            sentNum += ret;
            leftNum -= ret;
        }
    } while (leftNum > 0 && errno == EAGAIN);

    if (leftNum > 0) {
        WARN_LOG("KcpMessageTerminal::Connection::rawOut -- write fd %d ret %d errno %d (%s)\n",
                   conn->fd_, ret, errno, strerror(errno));
        return -1;
    }

    return sentNum;
}

KcpMessageTerminal::KcpMessageTerminal(bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial): MessageTerminal(needHB, enableSendCRC, enableRecvCRC, enableSerial) {
    
}

corpc::Connection *KcpMessageTerminal::buildConnection(int fd, IO *io, Worker *worker) {
    return new KcpMessageTerminal::Connection(fd, io, worker, this);
}

KcpPipeline::KcpPipeline(std::shared_ptr<Connection> &connection, Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize, uint bodySizeOffset, SIZE_TYPE bodySizeType): corpc::MessagePipeline(connection, worker, decodeFun, encodeFun, headSize, maxBodySize), data_(CORPC_MAX_KCP_PACKAGE_SIZE,0), bodySizeOffset_(bodySizeOffset), bodySizeType_(bodySizeType), headNum_(0), bodyNum_(0) {
    dataBuf_ = (uint8_t *)data_.data();
}

bool KcpPipeline::upflow(uint8_t *buf, int size) {
    std::shared_ptr<corpc::Connection> connection = connection_.lock();
    std::shared_ptr<KcpMessageTerminal::Connection> kcpCon = std::static_pointer_cast<KcpMessageTerminal::Connection>(connection);
    assert(kcpCon);

    // 注意：
    //   当本端是服务端时
    //      如果收到CORPC_MSG_TYPE_UDP_HANDSHAKE_3消息，说明对端可能仍在握手状态，需要重发CORPC_MSG_TYPE_UDP_HANDSHAKE_4给对端
    //      在对端还未离开握手状态时，服务端可能会先发出通过kcp包装的心跳消息，导致对端出错关闭并导致本端
    //   当本端是服务器或者客户端时
    //      收到握手消息可以直接丢弃（因为这些消息是重复发送延迟到达了）
    // 由于握手消息是22字节，kcp消息是大于24字节，因此这里直接通过消息长度来判断
    if (size == CORPC_MESSAGE_HEAD_SIZE) {
        int32_t msgtype;
        std::memcpy(&msgtype, buf + 4, sizeof(msgtype));
        msgtype = be32toh(msgtype);

        if (msgtype == CORPC_MSG_TYPE_UDP_HANDSHAKE_3) {
            uint8_t shakemsg4buf[CORPC_MESSAGE_HEAD_SIZE] = {0};
            int32_t tmp = htobe32(CORPC_MSG_TYPE_UDP_HANDSHAKE_4);
            std::memcpy(shakemsg4buf + 4, &tmp, sizeof(tmp));

            int ret = (int)::write(connection->getfd(), shakemsg4buf, CORPC_MESSAGE_HEAD_SIZE);
            if (ret != CORPC_MESSAGE_HEAD_SIZE) {
                ERROR_LOG("KcpPipeline::upflow -- send shakemsg4 failed\n");
                return false;
            }
        }

        return true;
    }
    
    int ret = kcpCon->kcpInput((const char*)buf, size);
    if (ret < 0) {
        ERROR_LOG("KcpPipeline::upflow -- kcpInput failed\n");
        
        //return false;
    }

    while (true)
    {
        //kcp将接收到的kcp数据包还原成之前kcp发送的buffer数据
        ret = kcpCon->kcpRecv((char*)dataBuf_, CORPC_MAX_KCP_PACKAGE_SIZE);
        if (ret < 0) {
            break;
        }

        // 解析数据
        int dataSize = ret;
        int offset = 0;
        while (dataSize > offset) {
            // 先解析头部
            if (headNum_ < headSize_) {
                int needNum = headSize_ - headNum_;
                if (dataSize - offset >= needNum) {
                    memcpy(headBuf_ + headNum_, dataBuf_ + offset, needNum);
                    headNum_ = headSize_;
                    
                    offset += needNum;
                } else {
                    memcpy(headBuf_ + headNum_, dataBuf_ + offset, dataSize - offset);
                    headNum_ += dataSize - offset;
                    
                    break;
                }
            }
            
            if (!bodySize_) {
                // 解析消息长度值
                if (bodySizeType_ == TWO_BYTES) {
                    uint16_t x;
                    std::memcpy(&x, headBuf_ + bodySizeOffset_, sizeof(x));
                    bodySize_ = be16toh(x);
                } else {
                    assert(bodySizeType_ == FOUR_BYTES);
                    uint32_t x;
                    std::memcpy(&x, headBuf_ + bodySizeOffset_, sizeof(x));
                    bodySize_ = be32toh(x);
                }
                
                if (bodySize_ > maxBodySize_) { // 数据超长
                    ERROR_LOG("KcpPipeline::upflow -- request too large in thread\n");
                    
                    return false;
                }
            }
            
            // 从缓存中解析数据
            if (bodyNum_ < bodySize_) {
                int needNum = bodySize_ - bodyNum_;
                if (dataSize - offset >= needNum) {
                    memcpy(bodyBuf_ + bodyNum_, dataBuf_ + offset, needNum);
                    bodyNum_ = bodySize_;
                    
                    offset += needNum;
                } else {
                    memcpy(bodyBuf_ + bodyNum_, dataBuf_ + offset, dataSize - offset);
                    bodyNum_ += dataSize - offset;
                    
                    break;
                }
            }
            
            WorkerTask *task = decodeFun_(connection, headBuf_, bodyBuf_, bodySize_);
            
            if (connection->isDecodeError()) {
                return false;
            }
            
            if (task) {
                worker_->addTask(task);
            }
            
            // 处理完一个请求消息，复位状态
            headNum_ = 0;
            bodyNum_ = 0;
            bodySize_ = 0;
        }
    }

    kcpCon->kcpFlush();
    
    return true;
}

std::shared_ptr<corpc::Pipeline> KcpPipelineFactory::buildPipeline(std::shared_ptr<corpc::Connection> &connection) {
    return std::shared_ptr<corpc::Pipeline>( new corpc::KcpPipeline(connection, worker_, decodeFun_, encodeFun_, headSize_, maxBodySize_, bodySizeOffset_, bodySizeType_) );
}
