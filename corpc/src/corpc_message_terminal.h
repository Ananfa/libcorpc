/*
 * Created by Xianke Liu on 2024/6/28.
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

#ifndef corpc_message_terminal_h
#define corpc_message_terminal_h

#include "corpc_io.h"
#include "corpc_crypter.h"
#include "corpc_message_buffer.h"
#include <map>

#include <google/protobuf/message.h>

namespace corpc {
    class MessageTerminal {
        class MessageWorkerTask;
    public:
        class Connection: public corpc::Connection {
        public:
            Connection(int fd, IO *io, Worker *worker, MessageTerminal *terminal);
            virtual ~Connection();

            virtual void onConnect() override;
            virtual void onClose() override;
            
            MessageTerminal *getTerminal() { return terminal_; }
            Worker *getWorker() { return worker_; }
            Crypter *getCrypter() { return crypter_.get(); }
            void setCrypter(std::shared_ptr<Crypter> &crypter) { crypter_ = crypter; }
            std::shared_ptr<MessageBuffer> &getMsgBuffer() { return msgBuffer_; }
            void setMsgBuffer(std::shared_ptr<MessageBuffer> &msgBuffer) { msgBuffer_ = msgBuffer; }
            //uint64_t getCreateTime() { return createTime_; }
            void setRecvSerial(uint32_t serial) { recvSerial_ = serial; }
            uint32_t getRecvSerial() { return recvSerial_; }
            void setLastSendSerial(uint32_t serial) { lastSendSerial_ = serial; }
            uint32_t getLastSendSerial() { return lastSendSerial_; }

            // 以下3个方法都会对消息缓存操作，消息缓存不是线程安全的，需要在worker中处理
            void scrapMessages(uint32_t serial); // 擦除已确认消息
            void send(int32_t type, bool isRaw, bool needCrypt, bool needBuffer, uint16_t tag, std::shared_ptr<void> msg);
            void resend(); // 重发消息缓存中所有消息
        private:
            MessageTerminal *terminal_;
            Worker *worker_;
            std::shared_ptr<Crypter> crypter_;
            std::shared_ptr<MessageBuffer> msgBuffer_; // 已发送消息缓存（用于实现断线重连机制，在worker中处理才是安全的）
            //time_t createTime_;   // 连接创建时间
            uint32_t recvSerial_; // 接收消息序号（连接建立后从0开始，必须保持连续，包括心跳数据包，不连续则断线）
            uint32_t lastSendSerial_; // 最后发送消息序列号

        public:
            friend class MessageTerminal;
            friend class MessageTerminal::MessageWorkerTask;
        };
        
    private:
        typedef std::function<void(int32_t type, uint16_t tag, std::shared_ptr<google::protobuf::Message>, std::shared_ptr<Connection>)> MessageHandle;
        typedef std::function<void(int32_t type, uint16_t tag, std::shared_ptr<std::string>, std::shared_ptr<Connection>)> OtherMessageHandle;
        
        struct RegisterMessageInfo {
            google::protobuf::Message *proto;
            MessageHandle handle;
            bool needCoroutine;
            bool banned; // 屏蔽
        };
        
        class MessageWorkerTask: public corpc::WorkerTask {
        protected:
            MessageWorkerTask() {}
            virtual ~MessageWorkerTask() {}

        public:
            static MessageWorkerTask* create() {
                return new MessageWorkerTask();
            }

            void destory() {
                delete this;
            }

            static void *taskCallRoutine( void * arg );

            virtual void doTask() override;
            
        public:
            int32_t type; // 正数类型消息为proto消息，负数类型消息用于系统消息，如：建立连接(-1)、断开连接(-2)
            uint16_t tag; // 客户端向服务器发带tag消息，服务器对这消息应答消息也需带相同的tag（客户端会等待tag消息返回）
            uint32_t reqSerial; // 连接对方收到的最后消息序号
            bool banned; // 屏蔽
            std::shared_ptr<Connection> connection;  // 消息来源的连接，注意：当type为-1时表示新建立连接，当type为-2时表示断开的连接
            std::shared_ptr<void> msg; // 接收到的消息，注意：当type为-1或-2时，msg中无数据
        };

    public:
        MessageTerminal(bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial);
        virtual ~MessageTerminal();
        
        bool registerMessage(int32_t type,
                             google::protobuf::Message *proto,
                             bool needCoroutine,
                             MessageHandle handle);

        void setOtherMessageHandle(OtherMessageHandle handle) { otherMessageHandle_ = handle; };

        // 接收完整的封禁列表
        bool setBanMessages(std::list<int32_t> &msgTypes);
        
        virtual corpc::Connection * buildConnection(int fd, IO *io, Worker *worker);

        static WorkerTask* decode(std::shared_ptr<corpc::Connection> &connection, uint8_t *head, uint8_t *body, int size);
        
        static bool encode(std::shared_ptr<corpc::Connection> &connection, std::shared_ptr<void>& data, uint8_t *buf, int space, int &size, std::string &downflowBuf, uint32_t &downflowBufSentNum);
        
    protected:
        bool needHB_; // 是否进行心跳
        bool enableSendCRC_; // 是否需要发包时校验CRC码
        bool enableRecvCRC_; // 是否需要收包时校验CRC码
        bool enableSerial_;  // 是否需要消息序号
        std::map<int32_t, RegisterMessageInfo> registerMessageMap_;
        OtherMessageHandle otherMessageHandle_;  // 其他未注册消息的处理

    public:
        friend class MessageClient;
    };
}

#endif /* corpc_message_terminal_h */
