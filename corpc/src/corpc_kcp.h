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

#ifndef corpc_kcp_h
#define corpc_kcp_h

#include "corpc_message_server.h"
#include "corpc_mutex.h"
#include "ikcp.h"

namespace corpc {
    class KcpMessageServer: public MessageServer {
    public:
        class Connection: public MessageServer::Connection {
        public:
            Connection(int fd, MessageServer* server);
            virtual ~Connection();

            virtual void onSenderInit();

            void kcpUpdate(uint32_t current);
            uint32_t kcpCheck(uint32_t current);
            int kcpInput(const char *data, long size);
            int kcpSend(const char *buffer, int len);
            int kcpRecv(char *buffer, int len);
            void kcpFlush();

        protected:
            virtual ssize_t write(const void *buf, size_t nbyte);

        private:
            static void *updateRoutine( void * arg );

            static int rawOut(const char *buf, int len, ikcpcb *kcp, void *obj);

        private:
            ikcpcb* pkcp_;
            Mutex kcpMtx_; // pkcp_同步访问锁

        public:
            friend class KcpPipeline;
        };

    public:
        KcpMessageServer(corpc::IO *io, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial, const std::string& ip, uint16_t port);
        virtual ~KcpMessageServer() {}

    protected:
        virtual corpc::Connection * buildConnection(int fd);
    };

    class KcpPipeline: public MessagePipeline {
    public:
        KcpPipeline(std::shared_ptr<Connection> &connection, Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize, uint bodySizeOffset, SIZE_TYPE bodySizeType);
        virtual ~KcpPipeline() {}
        
        virtual bool upflow(uint8_t *buf, int size);

    private:
        std::string data_;
        uint8_t *dataBuf_;

        uint headNum_;
        uint bodyNum_;
        
        uint bodySizeOffset_;
        SIZE_TYPE bodySizeType_;
    };
    
    class KcpPipelineFactory: public MessagePipelineFactory {
    public:
        KcpPipelineFactory(Worker *worker, DecodeFunction decodeFun, EncodeFunction encodeFun, uint headSize, uint maxBodySize, uint bodySizeOffset, MessagePipeline::SIZE_TYPE bodySizeType): MessagePipelineFactory(worker, decodeFun, encodeFun, headSize, maxBodySize), bodySizeOffset_(bodySizeOffset), bodySizeType_(bodySizeType) {}
        ~KcpPipelineFactory() {}
        
        virtual std::shared_ptr<Pipeline> buildPipeline(std::shared_ptr<Connection> &connection);
        
    public:
        uint bodySizeOffset_;
        MessagePipeline::SIZE_TYPE bodySizeType_;
    };
    
}

#endif /* corpc_kcp_h */
