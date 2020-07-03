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

#include "corpc_timeout_list.h"

namespace corpc {

    TimeoutList::TimeoutList():m_pHeader(new Node(CORPC_MAX_UINT64, CORPC_SKIP_LIST_MAX_LEVEL)), m_pTail(new Node(CORPC_MIN_UINT64, CORPC_SKIP_LIST_MAX_LEVEL)), m_maxCurLevel(1) {
        for (int i = 0; i < CORPC_SKIP_LIST_MAX_LEVEL; i++) {
            m_pHeader->links[i].next = m_pTail;
            m_pTail->links[i].prev = m_pHeader;
        }
    }

    TimeoutList::~TimeoutList() {
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

    TimeoutList::Node* TimeoutList::insert(uint64_t id, uint64_t expireTime, std::shared_ptr<RpcClientTask>& rpcTask) {
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
            curNode = new Node(expireTime, id, 0, rpcTask);

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
            
            curNode = new Node(expireTime, id, newlevel, rpcTask);
            
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

    void TimeoutList::remove(Node* node) {
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

    TimeoutList::Node* TimeoutList::getLast() {
        Node* n = m_pTail->links[0].prev;
        if (n == m_pHeader) {
            return nullptr;
        }

        return n->link.prev;
    }

    TimeoutList::Node* TimeoutList::getNode(uint64_t id) {
        auto it = m_nodeMap.find(id);
        if (it != m_nodeMap.end()) {
            return it->second;
        }

        return nullptr;
    }
}