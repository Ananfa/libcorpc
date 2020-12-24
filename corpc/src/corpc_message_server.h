/*
 * Created by Xianke Liu on 2018/4/23.
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

#ifndef corpc_message_server_h
#define corpc_message_server_h

#include "corpc_io.h"
#include "corpc_crypter.h"
#include <map>

#include <google/protobuf/message.h>

namespace corpc {
    class MessageServer: public corpc::Server {
    public:
        class Connection: public corpc::Connection {
        public:
            Connection(int fd, MessageServer* server);
            virtual ~Connection();
            
            virtual void onClose();
            
            MessageServer *getServer() { return _server; }
            std::shared_ptr<Crypter> &getCrypter() { return _crypter; }
            void setCrypter(std::shared_ptr<Crypter> &crypter) { _crypter = crypter; }
            uint64_t getCreateTime() { return _createTime; }
            
            void send(int16_t type, bool isRaw, bool needCrypt, uint16_t tag, uint32_t serial, std::shared_ptr<void> msg);
        private:
            MessageServer *_server;
            std::shared_ptr<Crypter> _crypter;
            time_t _createTime;   // 连接创建时间
            uint32_t _recvSerial; // 接收消息序号（连接建立后从0开始，必须保持连续，包括心跳数据包，不连续则断线）
        public:
            friend class MessageServer;
        };
        
    private:
        typedef std::function<void(int16_t type, uint16_t tag, std::shared_ptr<google::protobuf::Message>, std::shared_ptr<Connection>)> MessageHandle;
        typedef std::function<void(int16_t type, uint16_t tag, std::shared_ptr<std::string>, std::shared_ptr<Connection>)> OtherMessageHandle;
        
        struct RegisterMessageInfo {
            int16_t type;
            google::protobuf::Message *proto;
            bool needCoroutine;
            bool banned; // 屏蔽
            MessageHandle handle;
        };
        
        struct WorkerTask {
            int16_t type; // 正数类型消息为proto消息，负数类型消息用于系统消息，如：建立连接(-1)、断开连接(-2)
            bool banned; // 屏蔽
            uint16_t tag; // 客户端向服务器发带tag消息，服务器对这消息应答消息也需带相同的tag（客户端会等待tag消息返回）
            std::shared_ptr<Connection> connection;  // 消息来源的连接，注意：当type为-1时表示新建立连接，当type为-2时表示断开的连接
            std::shared_ptr<void> msg; // 接收到的消息，注意：当type为-1或-2时，msg中无数据
        };
        
        
        class Worker: public corpc::CoroutineWorker {
        public:
            Worker(MessageServer *server): _server(server) {}
            virtual ~Worker() {}
            
        protected:
            static void *taskCallRoutine( void * arg );
            
            virtual void handleMessage(void *msg); // 注意：处理完消息需要自己删除msg
            
        private:
            MessageServer *_server;
        };
        
    public:
        MessageServer(IO *io, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial);
        virtual ~MessageServer() = 0;
        
        bool registerMessage(int type,
                             google::protobuf::Message *proto,
                             bool needCoroutine,
                             MessageHandle handle);

        void setOtherMessageHandle(OtherMessageHandle handle) { _otherMessageHandle = handle; };

        bool banMessage(int type);
        bool unbanMessage(int type);
        
        static void* decode(std::shared_ptr<corpc::Connection> &connection, uint8_t *head, uint8_t *body, int size);
        
        static bool encode(std::shared_ptr<corpc::Connection> &connection, std::shared_ptr<void>& data, uint8_t *buf, int space, int &size, std::string &downflowBuf, uint32_t &downflowBufSentNum);
        
        virtual bool start() { return corpc::Server::start(); }
    protected:
        virtual corpc::Connection * buildConnection(int fd);
        
        virtual void onConnect(std::shared_ptr<corpc::Connection>& connection);
        
        virtual void onClose(std::shared_ptr<corpc::Connection>& connection);
        
    protected:
        bool _needHB; // 是否进行心跳
        bool _enableSendCRC; // 是否需要发包时校验CRC码
        bool _enableRecvCRC; // 是否需要收包时校验CRC码
        bool _enableSerial;  // 是否需要消息序号
        std::map<int, RegisterMessageInfo> _registerMessageMap;
        OtherMessageHandle _otherMessageHandle;  // 其他未注册消息的处理

    public:
        friend class MessageServer::Connection;
    };
    
    class TcpMessageServer: public MessageServer {
    public:
        TcpMessageServer(corpc::IO *io, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, const std::string& ip, uint16_t port);
        virtual ~TcpMessageServer() {}
    };
    
    class UdpMessageServer: public MessageServer {
    public:
        UdpMessageServer(corpc::IO *io, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, const std::string& ip, uint16_t port);
        virtual ~UdpMessageServer() {}
    };
}

#endif /* corpc_message_server_h */
