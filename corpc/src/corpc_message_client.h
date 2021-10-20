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
#include <google/protobuf/message.h>
#include <memory>

namespace corpc {
    class MessageClient: public std::enable_shared_from_this<MessageClient> {
    public:
        MessageClient(bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, std::shared_ptr<Crypter> &crypter, uint32_t lastRecvSerial): _needHB(needHB), _enableSendCRC(enableSendCRC), _enableRecvCRC(enableRecvCRC), _enableSerial(enableSerial), _crypter(crypter), _lastRecvSerial(lastRecvSerial) {}
        virtual ~MessageClient();
        
        virtual bool start() = 0;
        void stop() { _running = false; }
        
        std::shared_ptr<MessageClient> getPtr() {
            return shared_from_this();
        }

        bool isRunning() { return _running; };
        
        void send(int16_t type, uint16_t tag, bool needCrypter, std::shared_ptr<google::protobuf::Message> msg);
        void recv(int16_t& type, uint16_t& tag, std::shared_ptr<google::protobuf::Message>& msg); // 收不到数据时type为0，msg为nullptr
        
        bool registerMessage(int16_t type, std::shared_ptr<google::protobuf::Message> proto);
        
        uint32_t getLastRecvSerial() { return _lastRecvSerial; }
        uint32_t getLastSendSerial() { return _lastSendSerial; }

    protected:
        void close();

    protected:
        struct MessageInfo {
            int16_t type;
            uint16_t tag;
            std::shared_ptr<google::protobuf::Message> proto;
            bool needCrypter;   // 发送消息时标记消息是否需要加密
        };
        
        bool _needHB;        // 是否需要心跳
        bool _enableSendCRC; // 是否需要发包时校验CRC码
        bool _enableRecvCRC; // 是否需要收包时校验CRC码
        bool _enableSerial;  // 是否需要消息序号

        uint32_t _lastRecvSerial = 0;
        uint32_t _lastSendSerial = 0;
        
        std::shared_ptr<Crypter> _crypter;

        int _s;
        
        MPSC_NoLockQueue<MessageInfo*> _sendQueue;
        MPSC_NoLockQueue<MessageInfo*> _recvQueue;
        
        std::map<int16_t, MessageInfo> _registerMessageMap;
        
        uint64_t _lastRecvHBTime = 0; // 最后一次收到心跳的时间
        uint64_t _lastSendHBTime = 0; // 最后一次发送心跳的时间

        bool _running = false;
    };

    class TcpClient: public MessageClient {
    public:
        TcpClient(const std::string& host, uint16_t port, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, std::shared_ptr<Crypter> &crypter, uint32_t lastRecvSerial = 0): MessageClient(needHB, enableSendCRC, enableRecvCRC, enableSerial, crypter, lastRecvSerial), _host(host), _port(port) {}
        ~TcpClient() {}
        
        virtual bool start();

    private:
        static void *workRoutine( void * arg ); // 数据收发协程

    private:
        std::string _host;
        uint16_t _port;
    };

    class UdpClient: public MessageClient {
    public:
        UdpClient(const std::string& host, uint16_t port, uint16_t local_port, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, std::shared_ptr<Crypter> crypter, uint32_t lastRecvSerial = 0): MessageClient(needHB, enableSendCRC, enableRecvCRC, enableSerial, crypter, lastRecvSerial), _host(host), _port(port), _local_port(local_port) {}
        ~UdpClient() {}

        virtual bool start();

    private:
        static void *workRoutine( void * arg ); // 数据收发协程
        
    private:
        std::string _host;
        uint16_t _port;
        uint16_t _local_port;
    };
}

#endif /* corpc_message_client_h */
