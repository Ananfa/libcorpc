//
//  co_rpc_client.cpp
//  rpccli
//
//  Created by Xianke Liu on 2017/11/1.
//  Copyright © 2017年 Dena. All rights reserved.
//
#include "co_rpc_routine_env.h"

#include "co_rpc_client.h"

namespace CoRpc {
    
    bool Client::registerChannel(Channel *channel) {
        if (_channelSet.find(channel) != _channelSet.end()) {
            return false;
        }
        
        _channelSet.insert(std::make_pair(channel, channel));
        return true;
    }
    
    void Client::start() {
        RoutineEnvironment::startCoroutine(downQueueRoutine, this);
        
        co_register_fd(_upQueue.getWriteFd());
        
        _t = std::thread(threadEntry, this);
    }
    
    void Client::threadEntry(Client *self) {
        RoutineEnvironment *curenv = RoutineEnvironment::getEnv();
        assert(curenv);
        
        // 启动channel deamon协程
        for (ChannelSet::iterator it = self->_channelSet.begin(); it != self->_channelSet.end(); it++) {
            it->first->start(curenv->getAttr());
        }
        
        co_register_fd(self->_downQueue.getWriteFd());
        
        // 启动上行任务队列处理协程
        stCoRoutine_t *co = RoutineEnvironment::startCoroutine(upQueueRoutine, self);
        if (!co) {
            printf("Error: Client::threadEntry can't start upQueueRoutine at once!\n");
        }
        
        RoutineEnvironment::runEventLoop();
    }
    
    void *Client::upQueueRoutine( void * arg ) {
        Client *self = (Client *)arg;
        co_enable_hook_sys();
        int readFd = self->_upQueue.getReadFd();
        co_register_fd(readFd);
        
        int ret;
        int hopeNum = 1;
        while (true) {
            std::vector<char> buf(hopeNum);
            int total_read_num = 0;
            while (total_read_num < hopeNum) {
                ret = read(readFd, &buf[0] + total_read_num, hopeNum - total_read_num);
                assert(ret != 0);
                if (ret < 0) {
                    if (errno == EAGAIN) {
                        continue;
                    }
                    
                    // 管道出错
                    printf("Error: Client::upQueueRoutine read from up pipe fd %d ret %d errno %d (%s)\n",
                           readFd, ret, errno, strerror(errno));
                    
                    // TODO: 如何处理？退出协程？
                    // sleep 10 milisecond
                    struct pollfd pf = { 0 };
                    pf.fd = -1;
                    poll( &pf,1,10);
                    
                    break;
                }
                
                total_read_num += ret;
            }
            
            hopeNum = 0;
            // 处理任务队列
            ClientRpcTask *task = self->_upQueue.pop();
            while (task) {
                hopeNum++;
                
                // 先从channel中获得一connection
                Connection *conn = task->channel->getNextConnection();
                
                if (conn->_st == Connection::CLOSED) {
                    // 返回服务器断接错误（通过controller）
                    task->controller->SetFailed("Server not connected");
                    
                    // 将任务插入到下行队列中
                    self->_downQueue.push(task);
                } else {
                    conn->_waitSendTaskCoList.push_back(task);
                    
                    if (conn->_deamonHang) {
                        co_resume(conn->_deamonCo);
                    }
                }
                
                task = self->_upQueue.pop();
            }
            
            if (hopeNum == 0) {
                printf("Warning: Client::upQueueRoutine no task in queue\n");
                
                hopeNum = 1;
            }
        }
        
        return NULL;
    }
    
    void *Client::downQueueRoutine( void * arg ) {
        Client *self = (Client *)arg;
        co_enable_hook_sys();
        int readFd = self->_downQueue.getReadFd();
        co_register_fd(readFd);
        
        int ret;
        int hopeNum = 1;
        while (true) {
            std::vector<char> buf(hopeNum);
            int total_read_num = 0;
            while (total_read_num < hopeNum) {
                ret = read(readFd, &buf[0] + total_read_num, hopeNum - total_read_num);
                assert(ret != 0);
                if (ret < 0) {
                    if (errno == EAGAIN) {
                        continue;
                    }
                    
                    // 管道出错
                    printf("Error: Client::downQueueRoutine read from up pipe fd %d ret %d errno %d (%s)\n",
                           readFd, ret, errno, strerror(errno));
                    
                    // TODO: 如何处理？退出协程？
                    // sleep 10 milisecond
                    struct pollfd pf = { 0 };
                    pf.fd = -1;
                    poll( &pf,1,10);
                    
                    break;
                }
                
                total_read_num += ret;
            }
            
            hopeNum = 0;
            // 处理任务队列
            ClientRpcTask *task = self->_downQueue.pop();
            while (task) {
                hopeNum++;
                
                co_resume(task->co);
                
                task = self->_downQueue.pop();
            }
            
            if (hopeNum == 0) {
                printf("Warning: Client::downQueueRoutine no task in queue\n");
                
                hopeNum = 1;
            }
        }
        
        return NULL;
    }
}
