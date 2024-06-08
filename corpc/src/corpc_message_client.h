/*
 * Created by Xianke Liu on 2021/6/18.
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

#ifndef corpc_message_client_h
#define corpc_message_client_h

#include "corpc_define.h"
#include "corpc_crypter.h"
#include "ikcp.h"
#include <google/protobuf/message.h>
#include <memory>

namespace corpc {
    class MessageClient: public std::enable_shared_from_this<MessageClient> {
    public:
        MessageClient(bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, std::shared_ptr<Crypter> &crypter, uint32_t lastRecvSerial): needHB_(needHB), enableSendCRC_(enableSendCRC), enableRecvCRC_(enableRecvCRC), enableSerial_(enableSerial), crypter_(crypter), lastRecvSerial_(lastRecvSerial) {}
        virtual ~MessageClient();
        
        virtual bool start() = 0;
        void stop() { running_ = false; } // TODO: 发个结束消息
        
        std::shared_ptr<MessageClient> getPtr() {
            return shared_from_this();
        }

        bool isRunning() { return running_; };
        
        void send(int16_t type, uint16_t tag, bool needCrypter, std::shared_ptr<google::protobuf::Message> msg);
        void recv(int16_t& type, uint16_t& tag, std::shared_ptr<google::protobuf::Message>& msg); // 收不到数据时type为0，msg为nullptr
        
        bool registerMessage(int16_t type, std::shared_ptr<google::protobuf::Message> proto);
        
        uint32_t getLastRecvSerial() { return lastRecvSerial_; }
        uint32_t getLastSendSerial() { return lastSendSerial_; }

    protected:
        void close();

    protected:
        struct MessageInfo {
            int16_t type;
            uint16_t tag;
            std::shared_ptr<google::protobuf::Message> proto;
            bool needCrypter;   // 发送消息时标记消息是否需要加密
        };
        
        bool needHB_;        // 是否需要心跳
        bool enableSendCRC_; // 是否需要发包时校验CRC码
        bool enableRecvCRC_; // 是否需要收包时校验CRC码
        bool enableSerial_;  // 是否需要消息序号

        uint32_t lastRecvSerial_ = 0;
        uint32_t lastSendSerial_ = 0;
        
        std::shared_ptr<Crypter> crypter_;

        int s_;
        
        Co_MPSC_NoLockQueue<MessageInfo*> sendQueue_;
        MPSC_NoLockQueue<MessageInfo*> recvQueue_;
        
        std::map<int16_t, MessageInfo> registerMessageMap_;
        
        uint64_t lastRecvHBTime_ = 0; // 最后一次收到心跳的时间
        uint64_t lastSendHBTime_ = 0; // 最后一次发送心跳的时间

        bool running_ = false;

        //static uint8_t _heartbeatmsg[CORPC_MESSAGE_HEAD_SIZE];
    };

    class TcpClient: public MessageClient {
    public:
        TcpClient(const std::string& host, uint16_t port, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, std::shared_ptr<Crypter> &crypter, uint32_t lastRecvSerial = 0): MessageClient(needHB, enableSendCRC, enableRecvCRC, enableSerial, crypter, lastRecvSerial), host_(host), port_(port) {}
        ~TcpClient() {}
        
        virtual bool start();

    private:
        static void *workRoutine( void * arg ); // 数据收发协程

    private:
        std::string host_;
        uint16_t port_;
    };

    class UdpClient: public MessageClient {
    public:
        UdpClient(const std::string& host, uint16_t port, uint16_t local_port, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, std::shared_ptr<Crypter> crypter, uint32_t lastRecvSerial = 0): MessageClient(needHB, enableSendCRC, enableRecvCRC, enableSerial, crypter, lastRecvSerial), host_(host), port_(port), local_port_(local_port) {}
        ~UdpClient() {}

        virtual bool start();

    private:
        static void *workRoutine( void * arg ); // 数据收发协程
        
    private:
        std::string host_;
        uint16_t port_;
        uint16_t local_port_;
    };

    class KcpClient: public MessageClient {
    public:
        KcpClient(const std::string& host, uint16_t port, uint16_t local_port, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, std::shared_ptr<Crypter> crypter, uint32_t lastRecvSerial = 0);
        ~KcpClient();

        virtual bool start();

        ssize_t write(const void *buf, size_t nbyte);
    private:
        //static void *workRoutine(void *arg); // 数据收发协程

        static void *recvRoutine(void *arg); // 数据接收协程
        static void *sendRoutine(void *arg); // 数据发送协程
        static void *updateRoutine(void *arg); // 心跳协程
        
        static int rawOut(const char *buf, int len, ikcpcb *kcp, void *obj);
    private:
        std::string host_;
        uint16_t port_;
        uint16_t local_port_;

        ikcpcb* pkcp_;
    };
}

#endif /* corpc_message_client_h */
