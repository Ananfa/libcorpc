/*
 * Created by Xianke Liu on 2020/12/24.
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
#include "corpc_message_buffer.h"

#include <sys/time.h>

using namespace corpc;

void MessageBuffer::insertMessage(std::shared_ptr<SendMessageInfo> &msg) {
    msg->serial = ++lastSendSerial_;

    if (needBuf_) {
        BufMessageLink::Node *node = new BufMessageLink::Node();
        node->data = msg;

        bufMsglink_.push_back(node);
        bufMsgMap_.insert(std::make_pair(msg->serial, node));
    }
}

void MessageBuffer::traverse(MessageBuffer::MessageHandle handle) {
    if (needBuf_) {
        for (auto it = bufMsglink_.begin(); it != bufMsglink_.end(); ++it) {
            if (!handle(it->data)) {
                return;
            }
        }
    }
}

void MessageBuffer::scrapMessages(uint32_t serial) {
    if (needBuf_) {
        auto it = bufMsgMap_.find(serial);
        if (it != bufMsgMap_.end()) {
            BufMessageLink::Node *node = it->second;

            for (auto it1 = bufMsglink_.begin(); it1 != bufMsglink_.end(); ++it1) {
                bufMsgMap_.erase(it1->data->serial);

                if (it1->data->serial == serial) {
                    break;
                }
            }
            
            bufMsglink_.eraseTo(node);
        }
    }
}
