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
#include "corpc_message_buffer.h"
#include "corpc_message_terminal.h"
#include "corpc_kcp.h"
#include <map>

#include <google/protobuf/message.h>

namespace corpc {
    class MessageServer: public corpc::Server {
    public:
        MessageServer(IO *io, Worker *worker, MessageTerminal *terminal);
        virtual ~MessageServer() = 0;
        
        virtual bool start() { return corpc::Server::start(); }
    protected:
        virtual corpc::Connection * buildConnection(int fd);
        
    protected:
        MessageTerminal *terminal_;
    };
    
    class TcpMessageServer: public MessageServer {
    public:
        TcpMessageServer(IO *io, Worker *worker, MessageTerminal *terminal, const std::string& ip, uint16_t port);
        virtual ~TcpMessageServer() {}
    };
    
    class UdpMessageServer: public MessageServer {
    public:
        UdpMessageServer(IO *io, Worker *worker, MessageTerminal *terminal, const std::string& ip, uint16_t port);
        virtual ~UdpMessageServer() {}
    };

    class KcpMessageServer: public MessageServer {
    public:
        KcpMessageServer(corpc::IO *io, Worker *worker, KcpMessageTerminal *terminal, const std::string& ip, uint16_t port);
        virtual ~KcpMessageServer() {}
    };

}

#endif /* corpc_message_server_h */
