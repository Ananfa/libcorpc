/*
 * Created by Xianke Liu on 2018/4/3.
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

#include "corpc_routine_env.h"
#include "corpc_message_server.h"
#include "corpc_crc.h"

using namespace corpc;

MessageServer::MessageServer(IO *io, Worker *worker, MessageTerminal *terminal): corpc::Server(io), terminal_(terminal) {
    if (worker) {
        worker_ = worker;
    } else {
        worker_ = io->getWorker();
    }
}

MessageServer::~MessageServer() {

}

corpc::Connection *MessageServer::buildConnection(int fd) {
    return terminal_->buildConnection(fd, io_, worker_);
}

TcpMessageServer::TcpMessageServer(IO *io, Worker *worker, MessageTerminal *terminal, const std::string& ip, uint16_t port): MessageServer(io, worker, terminal) {
    acceptor_ = new TcpAcceptor(this, ip, port);
    
    pipelineFactory_.reset(new TcpPipelineFactory(worker_, MessageTerminal::decode, MessageTerminal::encode, CORPC_MESSAGE_HEAD_SIZE, CORPC_MAX_MESSAGE_SIZE, 0, MessagePipeline::FOUR_BYTES));
}

UdpMessageServer::UdpMessageServer(IO *io, Worker *worker, MessageTerminal *terminal, const std::string& ip, uint16_t port): MessageServer(io, worker, terminal) {
    acceptor_ = new UdpAcceptor(this, ip, port);
    
    pipelineFactory_.reset(new UdpPipelineFactory(worker_, MessageTerminal::decode, MessageTerminal::encode, CORPC_MESSAGE_HEAD_SIZE, CORPC_MAX_UDP_MESSAGE_SIZE));
}

KcpMessageServer::KcpMessageServer(corpc::IO *io, Worker *worker, KcpMessageTerminal *terminal, const std::string& ip, uint16_t port): MessageServer(io, worker, terminal) {
    acceptor_ = new UdpAcceptor(this, ip, port);

    pipelineFactory_.reset(new KcpPipelineFactory(worker_, MessageTerminal::decode, MessageTerminal::encode, CORPC_MESSAGE_HEAD_SIZE, CORPC_MAX_MESSAGE_SIZE, 0, corpc::MessagePipeline::FOUR_BYTES));
}
