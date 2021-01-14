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
    msg->serial = ++_lastSendSerial;

    if (_needBuf) {
        BufMessageLink::Node *node = new BufMessageLink::Node();
        node->data = msg;

        _bufMsglink.push_back(node);
        _bufMsgMap.insert(std::make_pair(msg->serial, node));
    }
}

void MessageBuffer::traversal(MessageBuffer::MessageHandle handle) {
    if (_needBuf) {
        for (auto it = _bufMsglink.begin(); it != _bufMsglink.end(); it++) {
            if (!handle(it->data)) {
                return;
            }
        }
    }
}

void MessageBuffer::scrapMessages(uint32_t serial) {
    if (_needBuf) {
        auto it = _bufMsgMap.find(serial);
        if (it != _bufMsgMap.end()) {
            BufMessageLink::Node *node = it->second;

            for (auto it1 = _bufMsglink.begin(); it1 != _bufMsglink.end(); it1++) {
                _bufMsgMap.erase(it1->data->serial);

                if (it1->data->serial == serial) {
                    break;
                }
            }
            
            _bufMsglink.eraseTo(node);
        }
    }
}
