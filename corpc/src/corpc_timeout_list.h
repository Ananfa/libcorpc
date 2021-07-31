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

namespace corpc {

    // 说明：
    // TimeoutList用于记录正在进行的RPC请求的超时时间并排序
    // 使用跳表数据结构，同时间超时的节点会记录到环形双向链中
    // 跳表的头部和尾部节点为时间无穷大和0的节点，理论上不会有其他节点跟他们同时
    // 从跳表中删除节点时，若该节点有同时环形双向链，则将链中下一个节点升级为跳表节点
    template <typename T>
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
            
            Node(uint64_t expireTime, uint64_t id, int level, T& data):expireTime(expireTime), id(id), data(data), link(nullptr, nullptr) {
                links.reserve(level);
                for (int i = 0; i < level; i++) {
                    links.emplace_back(nullptr, nullptr);
                }

                link.next = link.prev = this;
            };
            
        public:
            uint64_t expireTime;
            uint64_t id;

            T data;
            
            // 不同超时时间链（跳表）
            std::vector<Link> links;

            // 相同超时时间链（双向环，头部为跳表中节点）
            Link link;
        };

    public:
        TimeoutList();
        virtual ~TimeoutList();

        Node* insert(uint64_t id, uint64_t expireTime, T& data);
        void remove(Node* node);
        Node* getLast();
        Node* getNode(uint64_t id);
        bool empty();

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

    template <typename T>
    TimeoutList<T>::TimeoutList():m_pHeader(new Node(CORPC_MAX_UINT64, CORPC_SKIP_LIST_MAX_LEVEL)), m_pTail(new Node(CORPC_MIN_UINT64, CORPC_SKIP_LIST_MAX_LEVEL)), m_maxCurLevel(1) {
        for (int i = 0; i < CORPC_SKIP_LIST_MAX_LEVEL; i++) {
            m_pHeader->links[i].next = m_pTail;
            m_pTail->links[i].prev = m_pHeader;
        }
    }

    template <typename T>
    TimeoutList<T>::~TimeoutList() {
        Node* n = m_pHeader->links[0].next;

        while (n != m_pTail) {
            Node* n1 = n;
            n = n->links[0].next;

            Node* n2 = n1->link.next;
            while (n2 != n1) {
                Node* n3 = n2->link.next;
                delete n2;
                n2 = n3;
            }

            delete n1;
        }
        
        delete m_pHeader;
        delete m_pTail;
    }

    template <typename T>
    typename TimeoutList<T>::Node* TimeoutList<T>::insert(uint64_t id, uint64_t expireTime, T& data) {
        Node* prevNodes[CORPC_SKIP_LIST_MAX_LEVEL];
        Node* curNode = m_pHeader;
        
        for (int i = m_maxCurLevel - 1; i >= 0; i--) {
            Node* nextNode = curNode->links[i].next;
            while (nextNode->expireTime >= expireTime) {
                assert(nextNode->id != id);
                
                curNode = nextNode;
                nextNode = curNode->links[i].next;
            }
            
            prevNodes[i] = curNode;
        }
        
        // 如果是相同超时时间则加到双向环中，否则加到跳表中
        if (curNode->expireTime == expireTime) {
            // 加到链尾
            Node* prevNode = curNode->link.prev;
            Node* nextNode = curNode;
            curNode = new Node(expireTime, id, 0, data);

            curNode->link.next = nextNode;
            curNode->link.prev = prevNode;
            nextNode->link.prev = curNode;
            prevNode->link.next = curNode;
        } else {
            int newlevel = randomLevel();
            if (newlevel > m_maxCurLevel) {
                for (int i = m_maxCurLevel; i < newlevel; i++) {
                    prevNodes[i] = m_pHeader;
                }
                
                m_maxCurLevel = newlevel;
            }
            
            curNode = new Node(expireTime, id, newlevel, data);
            
            for (int i = 0; i < newlevel; i++) {
                Node* prevNode = prevNodes[i];
                Node* nextNode = prevNode->links[i].next;
                
                curNode->links[i].next = nextNode;
                curNode->links[i].prev = prevNode;
                prevNode->links[i].next = curNode;
                nextNode->links[i].prev = curNode;
            }
        }

        m_nodeMap.insert(std::make_pair(id, curNode));
        
        return curNode;
    }

    template <typename T>
    void TimeoutList<T>::remove(Node* node) {
        if (node->links.size() == 0) {
            // 从双向环中删除
            assert(node->link.next != node && node->link.prev != node);
            Node* prevNode = node->link.prev;
            Node* nextNode = node->link.next;
            prevNode->link.next = nextNode;
            nextNode->link.prev = prevNode;
        } else {
            // 从跳表中删除
            if (node->link.next != node) {
                // 如果有相同超时时间的双向环中节点，将下一个节点升级为跳表节点并保持节点的跳表层次替换原节点
                // 先修改双向环
                Node* prevNode = node->link.prev;
                Node* nextNode = node->link.next;
                prevNode->link.next = nextNode;
                nextNode->link.prev = prevNode;

                // 升级nextNode替换node在跳表中的位置
                nextNode->links = std::move(node->links);
                int level = nextNode->links.size();
                for (int i = 0; i < level; i++) {
                    nextNode->links[i].prev->links[i].next = nextNode;
                    nextNode->links[i].next->links[i].prev = nextNode;
                }
            } else {
                // 从跳表中删除，最大层次会改变
                int level = node->links.size();
                for (int i = 0; i < level; i++) {
                    node->links[i].prev->links[i].next = node->links[i].next;
                    node->links[i].next->links[i].prev = node->links[i].prev;

                    if (i > 0 && node->links[i].prev == m_pHeader && node->links[i].next == m_pTail) {
                        m_maxCurLevel--;
                    }
                }
            }
        }

        m_nodeMap.erase(node->id);

        delete node;
    }

    template <typename T>
    typename TimeoutList<T>::Node* TimeoutList<T>::getLast() {
        Node* n = m_pTail->links[0].prev;
        if (n == m_pHeader) {
            return nullptr;
        }

        return n->link.prev;
    }

    template <typename T>
    typename TimeoutList<T>::Node* TimeoutList<T>::getNode(uint64_t id) {
        auto it = m_nodeMap.find(id);
        if (it != m_nodeMap.end()) {
            return it->second;
        }

        return nullptr;
    }

    template <typename T>
    bool TimeoutList<T>::empty() {
        return m_pHeader->links[0].next == m_pTail;
    }
}

#endif /* corpc_timeout_list_h */