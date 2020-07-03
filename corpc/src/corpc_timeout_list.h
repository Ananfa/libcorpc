/*
 * Created by Xianke Liu on 2020/6/28.
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

#ifndef corpc_timeout_list_h
#define corpc_timeout_list_h

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <vector>
#include <map>

#include "corpc_define.h"
#include "corpc_rpc_common.h"

namespace corpc {

    // 说明：
    // TimeoutList用于记录正在进行的RPC请求的超时时间并排序
    // 使用跳表数据结构，同时间超时的节点会记录到环形双向链中
    // 跳表的头部和尾部节点为时间无穷大和0的节点，理论上不会有其他节点跟他们同时
    // 从跳表中删除节点时，若该节点有同时环形双向链，则将链中下一个节点升级为跳表节点
    class TimeoutList {
    public:
        class Node
        {
        public:
            class Link
            {
            public:
                Link(Node* n, Node* p):next(n), prev(p) {}
            public:
                Node* next;
                Node* prev;
            };
            
            Node(uint64_t expireTime, int level):expireTime(expireTime), link(nullptr, nullptr) {
                links.reserve(level);
                for (int i = 0; i < level; i++) {
                    links.emplace_back(nullptr, nullptr);
                }

                link.next = link.prev = this;
            };
            
            Node(uint64_t expireTime, uint64_t id, int level, std::shared_ptr<RpcClientTask>& rpcTask):expireTime(expireTime), id(id), rpcTask(rpcTask), link(nullptr, nullptr) {
                links.reserve(level);
                for (int i = 0; i < level; i++) {
                    links.emplace_back(nullptr, nullptr);
                }

                link.next = link.prev = this;
            };
            
        public:
            uint64_t expireTime;
            uint64_t id;

            std::shared_ptr<RpcClientTask> rpcTask;
            
            // 不同超时时间链（跳表）
            std::vector<Link> links;

            // 相同超时时间链（双向环，头部为跳表中节点）
            Link link;
        };

    public:
        TimeoutList();
        virtual ~TimeoutList();

        Node* insert(uint64_t id, uint64_t expireTime, std::shared_ptr<RpcClientTask>& rpcTask);
        void remove(Node* node);
        Node* getLast();
        Node* getNode(uint64_t id);

    private:
        uint32_t randomLevel() {
            uint32_t level = 1;
            const uint32_t p = 0x3fff; // 0.25 * 0xffff;
            while ( level < CORPC_SKIP_LIST_MAX_LEVEL && (rand() & 0xffff) < p ) {
                level++;
            }
            return level;
        }

    private:
        // 跳表
        uint32_t m_maxCurLevel;
        Node* m_pHeader;
        Node* m_pTail;

        // 通过id查找节点
        std::map<uint64_t, Node*> m_nodeMap;
    };

}

#endif /* corpc_timeout_list_h */