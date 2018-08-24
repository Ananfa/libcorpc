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
            uint64_t getCreateTime() { return _createTime; }
            
            void send(int32_t type, bool isRaw, std::shared_ptr<void> msg);
        private:
            MessageServer *_server;
            uint64_t _createTime;   // 连接创建时间
        };
        
    private:
        typedef std::function<void(std::shared_ptr<google::protobuf::Message>, std::shared_ptr<Connection>)> MessageHandle;
        
        struct RegisterMessageInfo {
            int32_t type;
            google::protobuf::Message *proto;
            bool needCoroutine;
            MessageHandle handle;
        };
        
        struct WorkerTask {
            int32_t type; // 正数类型消息为proto消息，负数类型消息用于系统消息，如：建立连接(-1)、断开连接(-2)
            std::shared_ptr<Connection> connection;  // 消息来源的连接，注意：当type为-1时表示新建立连接，当type为-2时表示断开的连接
            std::shared_ptr<google::protobuf::Message> msg; // 接收到的消息，注意：当type为-1或-2时，msg中无数据
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
        MessageServer(IO *io, bool needHB);
        virtual ~MessageServer() = 0;
        
        bool registerMessage(int type,
                             google::protobuf::Message *proto,
                             bool needCoroutine,
                             MessageHandle handle);
        
        static void* decode(std::shared_ptr<corpc::Connection> &connection, uint8_t *head, uint8_t *body, int size);
        
        static bool encode(std::shared_ptr<corpc::Connection> &connection, std::shared_ptr<void>& data, uint8_t *buf, int space, int &size, std::string &downflowBuf, uint32_t &downflowBufSentNum);
        
        virtual bool start() { return corpc::Server::start(); }
    protected:
        virtual corpc::Connection * buildConnection(int fd);
        
        virtual void onConnect(std::shared_ptr<corpc::Connection>& connection);
        
        virtual void onClose(std::shared_ptr<corpc::Connection>& connection);
        
    protected:
        bool _needHB; // 是否进行心跳
        
        std::map<int, RegisterMessageInfo> _registerMessageMap;
    };
    
    class TcpMessageServer: public MessageServer {
    public:
        TcpMessageServer(corpc::IO *io, bool needHB, const std::string& ip, uint16_t port);
        virtual ~TcpMessageServer() {}
    };
    
    class UdpMessageServer: public MessageServer {
    public:
        UdpMessageServer(corpc::IO *io, bool needHB, const std::string& ip, uint16_t port);
        virtual ~UdpMessageServer() {}
    };
}

#endif /* corpc_message_server_h */
