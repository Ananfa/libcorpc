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

#ifndef message_buffer_h
#define message_buffer_h

#include "corpc_define.h"
#include <string>
#include <functional>

namespace corpc {

    // 消息缓存用于产生消息序号以及缓存消息，可通过开关开启缓存功能
    // 注意： MessageBuffer不是线程安全的
    // 消息缓存中的消息序号应该是连续的且是循环使用的(next=(current+1)%0xffffffff)
    // 客户端消息发给服务器的消息（包括心跳消息）中带有所收到服务器最大消息序号，服务器收到消息后，会将此消息及之前消息删除，保证消息缓存消息数不会太大
    // 消息序号共有0x100000000个，使用中不会出现序号相同的两个消息（若有重复此时缓存消息数大于0x100000000个，不应出现此情况）
    class MessageBuffer
    {
        typedef NormalLink<SendMessageInfo> BufMessageLink;
        typedef std::function<bool(std::shared_ptr<SendMessageInfo>&)> MessageHandle;
        
    public:
        MessageBuffer(bool needBuf): needBuf_(needBuf), lastSendSerial_(0) {}
        ~MessageBuffer() {}

        // 插入新消息，并为新消息添加消息序号（注意：心跳消息以及网络控制消息不会加到消息缓存中，且不需要消息序号）
        void insertMessage(std::shared_ptr<SendMessageInfo> &msg);

        // 遍历所有消息执行handle，如果handle返回false则中断遍历
        void traverse(MessageHandle handle);

        // 删除某个消息序号之前的所有消息（包括消息序号消息）
        void scrapMessages(uint32_t serial);

        bool needBuf() { return needBuf_; }
    private:
        BufMessageLink bufMsglink_; // 缓存消息链
        std::map<uint64_t, BufMessageLink::Node*> bufMsgMap_; // 按消息序号索引链中消息节点

        bool needBuf_; // 是否缓存消息
        uint64_t lastSendSerial_; // 最后发送消息序列号
    };

}

#endif /* message_buffer_h */
