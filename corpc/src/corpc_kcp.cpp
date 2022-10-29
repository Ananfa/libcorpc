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

KcpMessageServer::Connection::Connection(int fd, MessageServer* server): MessageServer::Connection(fd, server) {
    _pkcp = ikcp_create(0x1, (void *)this);
    ikcp_nodelay(_pkcp, 1, 20, 2, 1);
    _pkcp->output = rawOut;
}

KcpMessageServer::Connection::~Connection() {
    DEBUG_LOG("KcpMessageServer::Connection::~Connection -- fd:%d\n", _fd);
    ikcp_release(_pkcp);
}

ssize_t KcpMessageServer::Connection::write(const void *buf, size_t nbyte) {
    // 注意：这里分割数据，方便设定接收缓冲区大小
    int ret;
    uint32_t sentNum = 0;
    uint32_t leftNum = nbyte;
//DEBUG_LOG("KcpMessageServer::Connection::write, nbyte:%d\n", nbyte);
    do {
        uint32_t pkgSize = (leftNum > CORPC_MAX_KCP_PACKAGE_SIZE)?CORPC_MAX_KCP_PACKAGE_SIZE:leftNum;

        ret = kcpSend((const char *)(buf + sentNum), pkgSize);
        if (ret < 0) {
            WARN_LOG("KcpMessageServer::Connection::write -- kcpSend ret %d\n", ret);

            return ret;
        }

        sentNum += pkgSize;
        leftNum -= pkgSize;
    } while(leftNum > 0);

    kcpFlush();

    return nbyte;
}

void KcpMessageServer::Connection::onSenderInit() {
    RoutineEnvironment::startCoroutine(updateRoutine, this);
}

void KcpMessageServer::Connection::kcpUpdate(uint32_t current) {
    LockGuard lock(_kcpMtx);
    ikcp_update(_pkcp, current);
}

uint32_t KcpMessageServer::Connection::kcpCheck(uint32_t current) {
    LockGuard lock(_kcpMtx);
    return ikcp_check(_pkcp, current);
}

int KcpMessageServer::Connection::kcpInput(const char *data, long size) {
    LockGuard lock(_kcpMtx);
    return ikcp_input(_pkcp, data, size);
}

int KcpMessageServer::Connection::kcpSend(const char *buffer, int len) {
    LockGuard lock(_kcpMtx);
    return ikcp_send(_pkcp, buffer, len);
}

int KcpMessageServer::Connection::kcpRecv(char *buffer, int len) {
    LockGuard lock(_kcpMtx);
    return ikcp_recv(_pkcp, buffer, len);
}

void KcpMessageServer::Connection::kcpFlush() {
    LockGuard lock(_kcpMtx);
    ikcp_flush(_pkcp);
}

void * KcpMessageServer::Connection::updateRoutine( void * arg ) {
    std::shared_ptr<KcpMessageServer::Connection> connection = std::static_pointer_cast<KcpMessageServer::Connection>(((KcpMessageServer::Connection*)arg)->getPtr());

    while (true) {
        if (connection->_isClosing) {
            break;
        }
//DEBUG_LOG("KcpMessageServer::Connection::updateRoutine -- ikcp_update\n");
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

    DEBUG_LOG("KcpMessageServer::Connection::updateRoutine ended, fd:%d\n", connection->_fd);
    return NULL;
}

int KcpMessageServer::Connection::rawOut(const char *buf, int len, ikcpcb *kcp, void *obj) {
    KcpMessageServer::Connection* conn = (KcpMessageServer::Connection*)obj;
    int ret;
    uint32_t sentNum = 0;
    uint32_t leftNum = len;
//DEBUG_LOG("KcpMessageServer::Connection::rawOut, len:%d\n", len);
    do {
        ret = (int)::write(conn->_fd, buf + sentNum, leftNum);
        if (ret > 0) {
            assert(ret <= leftNum);
            sentNum += ret;
            leftNum -= ret;
        }
    } while (leftNum > 0 && errno == EAGAIN);

    if (leftNum > 0) {
        WARN_LOG("KcpMessageServer::Connection::rawOut -- write fd %d ret %d errno %d (%s)\n",
                   conn->_fd, ret, errno, strerror(errno));
        return -1;
    }

    return sentNum;
}

KcpMessageServer::KcpMessageServer(corpc::IO *io, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, const std::string& ip, uint16_t port): MessageServer(io, needHB, enableSendCRC, enableRecvCRC, enableSerial) {
    _acceptor = new UdpAcceptor(this, ip, port);
    
    _pipelineFactory = new KcpPipelineFactory(_worker, decode, encode, CORPC_MESSAGE_HEAD_SIZE, CORPC_MAX_MESSAGE_SIZE, 0, corpc::MessagePipeline::FOUR_BYTES);
}

corpc::Connection *KcpMessageServer::buildConnection(int fd) {
    return new KcpMessageServer::Connection(fd, this);
}


KcpPipeline::KcpPipeline(std::shared_ptr<Connection> &connection, Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize, uint bodySizeOffset, SIZE_TYPE bodySizeType): corpc::MessagePipeline(connection, worker, decodeFun, encodeFun, headSize, maxBodySize), _data(CORPC_MAX_KCP_PACKAGE_SIZE,0), _bodySizeOffset(bodySizeOffset), _bodySizeType(bodySizeType), _headNum(0), _bodyNum(0) {
    _dataBuf = (uint8_t *)_data.data();
}

bool KcpPipeline::upflow(uint8_t *buf, int size) {
    std::shared_ptr<corpc::Connection> connection = _connection.lock();
    std::shared_ptr<KcpMessageServer::Connection> kcpCon = std::static_pointer_cast<KcpMessageServer::Connection>(connection);
    assert(kcpCon);
    
    int ret = kcpCon->kcpInput((const char*)buf, size);
    if (ret < 0) {
        ERROR_LOG("KcpPipeline::upflow -- kcpInput failed\n");
        
        //return false;
    }

    while (true)
    {
        //kcp将接收到的kcp数据包还原成之前kcp发送的buffer数据
        ret = kcpCon->kcpRecv((char*)_dataBuf, CORPC_MAX_KCP_PACKAGE_SIZE);
        if (ret < 0) {
            if (ret == -1) {
                break;
            }

            ERROR_LOG("KcpPipeline::upflow -- kcpRecv failed, ret:%d\n", ret);
            return false;
        }

        // 解析数据
        int dataSize = ret;
        int offset = 0;
        while (dataSize > offset) {
            // 先解析头部
            if (_headNum < _headSize) {
                int needNum = _headSize - _headNum;
                if (dataSize - offset >= needNum) {
                    memcpy(_headBuf + _headNum, _dataBuf + offset, needNum);
                    _headNum = _headSize;
                    
                    offset += needNum;
                } else {
                    memcpy(_headBuf + _headNum, _dataBuf + offset, dataSize - offset);
                    _headNum += dataSize - offset;
                    
                    break;
                }
            }
            
            if (!_bodySize) {
                // 解析消息长度值
                if (_bodySizeType == TWO_BYTES) {
                    uint16_t x = *(uint16_t*)(_headBuf + _bodySizeOffset);
                    _bodySize = be16toh(x);
                } else {
                    assert(_bodySizeType == FOUR_BYTES);
                    uint32_t x = *(uint32_t*)(_headBuf + _bodySizeOffset);
                    _bodySize = be32toh(x);
                }
                
                if (_bodySize > _maxBodySize) { // 数据超长
                    ERROR_LOG("KcpPipeline::upflow -- request too large in thread\n");
                    
                    return false;
                }
            }
            
            // 从缓存中解析数据
            if (_bodyNum < _bodySize) {
                int needNum = _bodySize - _bodyNum;
                if (dataSize - offset >= needNum) {
                    memcpy(_bodyBuf + _bodyNum, _dataBuf + offset, needNum);
                    _bodyNum = _bodySize;
                    
                    offset += needNum;
                } else {
                    memcpy(_bodyBuf + _bodyNum, _dataBuf + offset, dataSize - offset);
                    _bodyNum += dataSize - offset;
                    
                    break;
                }
            }
            
            void *msg = _decodeFun(connection, _headBuf, _bodyBuf, _bodySize);
            
            if (connection->isDecodeError()) {
                return false;
            }
            
            if (msg) {
                _worker->addMessage(msg);
            }
            
            // 处理完一个请求消息，复位状态
            _headNum = 0;
            _bodyNum = 0;
            _bodySize = 0;
        }
    }

    kcpCon->kcpFlush();
    
    return true;
}

std::shared_ptr<corpc::Pipeline> KcpPipelineFactory::buildPipeline(std::shared_ptr<corpc::Connection> &connection) {
    return std::shared_ptr<corpc::Pipeline>( new corpc::KcpPipeline(connection, _worker, _decodeFun, _encodeFun, _headSize, _maxBodySize, _bodySizeOffset, _bodySizeType) );
}

