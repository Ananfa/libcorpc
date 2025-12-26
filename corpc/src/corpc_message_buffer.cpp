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

void MessageBuffer::reset(uint32_t serial) {
    broken_ = false;
    bufMsglink_.clear();
    bufMsgMap_.clear();

    lastSerial_ = lastScrapSerial_ = serial;
}

bool MessageBuffer::insertMessage(std::shared_ptr<SendMessageInfo> &msg) {
    if (!broken_) {
        if (maxMsgNum_ > 0 && bufMsgMap_.size() >= maxMsgNum_) {
            WARN_LOG("msg buff broken because overflow\n");
            // 此时应清理最旧消息
            auto it = bufMsglink_.begin();
            lastScrapSerial_ = it->data->serial;
            bufMsgMap_.erase(lastScrapSerial_);
            bufMsglink_.erase(it);
            //broken_ = true;
        }

        if (msg->serial != lastSerial_ + 1) {
            ERROR_LOG("msg buff broken because serial invalid: %d != %d + 1\n", msg->serial, lastSerial_);
            broken_ = true;
        }

        if (broken_) {
            bufMsglink_.clear();
            bufMsgMap_.clear();
            return false;
        }

        BufMessageLink::Node *node = new BufMessageLink::Node();
        node->data = msg;

        bufMsglink_.push_back(node);
        bufMsgMap_.insert(std::make_pair(msg->serial, node));

        lastSerial_ = msg->serial;

        return true;
    }

    return false;
}

bool MessageBuffer::jumpToSerial(uint32_t serial) {
    if (!broken_) {
        if (serial != lastSerial_ + 1) {
            ERROR_LOG("msg buff broken because serial invalid: %d != %d + 1\n", serial, lastSerial_);
            broken_ = true;
        }

        if (broken_) {
            bufMsglink_.clear();
            bufMsgMap_.clear();
            return false;
        }

        lastSerial_ = serial;

        return true;
    }

    return false;
}

void MessageBuffer::traverse(MessageBuffer::MessageHandle handle) {
    for (auto it = bufMsglink_.begin(); it != bufMsglink_.end(); ++it) {
        if (!handle(it->data)) {
            return;
        }
    }
}

void MessageBuffer::scrapMessages(uint32_t serial) {
    for (auto it1 = bufMsglink_.begin(); it1 != bufMsglink_.end();) {
        auto curIt = it1++;

        if (curIt->data->serial > serial) {
            break;
        }

        bufMsgMap_.erase(curIt->data->serial);
        bufMsglink_.erase(curIt);
    }

    if (lastScrapSerial_ < serial) {
        lastScrapSerial_ = serial;
    }
}
