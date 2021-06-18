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
#include <thread>

namespace corpc {
    class MessageClient {
    public:
        MessageClient(bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, std::shared_ptr<Crypter> &crypter): _needHB(needHB), _enableSendCRC(enableSendCRC), _enableRecvCRC(enableRecvCRC), _enableSerial(enableSerial), _crypter(crypter), _lastRecvHBTime(0), _lastSendHBTime(0) {}
        virtual ~MessageClient() {}
        
        virtual bool start() = 0;
        
        void join();
        
        void send(int16_t type, uint16_t tag, google::protobuf::Message* msg);
        void recv(int16_t& type, uint16_t &tag, google::protobuf::Message*& msg); // 收不到数据时type为0，msg为nullptr
        
        bool registerMessage(int16_t type, google::protobuf::Message *proto);
        
    protected:
        struct MessageInfo {
            int16_t type;
            uint16_t tag;
            google::protobuf::Message *proto;
        };
        
        bool _needHB;        // 是否需要心跳
        bool _enableSendCRC; // 是否需要发包时校验CRC码
        bool _enableRecvCRC; // 是否需要收包时校验CRC码
        bool _enableSerial;  // 是否需要消息序号
        
        std::shared_ptr<Crypter> _crypter;

        int _s;
        std::thread _t;
        
        SyncQueue<MessageInfo*> _sendQueue;
        SyncQueue<MessageInfo*> _recvQueue;
        
        std::map<int16_t, MessageInfo> _registerMessageMap;
        
        uint64_t _lastRecvHBTime; // 最后一次收到心跳的时间
        uint64_t _lastSendHBTime; // 最后一次发送心跳的时间
    };

    class TcpClient: public MessageClient {
    public:
        TcpClient(const std::string& host, uint16_t port, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, std::shared_ptr<Crypter> &crypter): MessageClient(needHB, enableSendCRC, enableRecvCRC, enableSerial, crypter), _host(host), _port(port) {}
        ~TcpClient() {}
        
        virtual bool start();

    private:
        static void threadEntry( TcpClient *self ); // 数据收发线程
        
    private:
        std::string _host;
        uint16_t _port;
    };

    class UdpClient: public MessageClient {
    public:
        UdpClient(const std::string& host, uint16_t port, uint16_t local_port, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, std::shared_ptr<Crypter> crypter): MessageClient(needHB, enableSendCRC, enableRecvCRC, enableSerial, crypter), _host(host), _port(port), _local_port(local_port) {}
        ~UdpClient() {}

        virtual bool start();

    private:
        static void threadEntry( UdpClient *self ); // 数据收发线程
        
    private:
        std::string _host;
        uint16_t _port;
        uint16_t _local_port;
    };
}

#endif /* corpc_message_client_h */