//
//  main.cpp
//  testpipe
//
//  Created by Xianke Liu on 2017/11/6.
//  Copyright © 2017年 Dena. All rights reserved.
//
#include "co_routine.h"

#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <thread>

//int co_register_fd(int fd);

struct PipeType {
    int pipefd[2];
};

static int SetNonBlock(int iSock)
{
    int iFlags;
    
    iFlags = fcntl(iSock, F_GETFL, 0);
    //iFlags |= O_NONBLOCK;
    //iFlags |= O_NDELAY;
    int ret = fcntl(iSock, F_SETFL, iFlags);
    return ret;
}

void pipeSendTask(void *arg) {
    //co_enable_hook_sys();
    PipeType *pipe = (PipeType *)arg;
    
    //fcntl(pipe->pipefd[1], F_SETFL, fcntl(pipe->pipefd[1], F_GETFL, 0));
    while (true) {
        //printf("pipeSendTask sleep 1 second\n");
        // sleep 1 second
        //struct pollfd pf = { 0 };
        //pf.fd = -1;
        //poll( &pf,1,1000);
        usleep(1);
        
        // send something to pipe
        printf("pipeSendTask try write A\n");
        char buf = 'A';
        write(pipe->pipefd[1], &buf, 1);
        
        printf("pipeSendTask try write B\n");
        buf = 'B';
        write(pipe->pipefd[1], &buf, 1);
        
        printf("pipeSendTask try write C\n");
        buf = 'C';
        write(pipe->pipefd[1], &buf, 1);
    }
    
    
    //return NULL;
}

void *pipeRecvTask(void *arg) {
    co_enable_hook_sys();
    PipeType *pipe = (PipeType *)arg;
    
    co_register_fd(pipe->pipefd[0]);
    
    //SetNonBlock( pipe->pipefd[0] );
    //SetNonBlock( pipe->pipefd[0] );
    int ret;
    char buf;
    while (true) {
        printf("pipeRecvTask try read\n");
        
        
        ret = read(pipe->pipefd[0], &buf, sizeof(buf));
        
        if (ret > 0) {
            printf("pipeRecvTask read %c\n", buf);
        }
    }
    
    return NULL;
}

int co_pipe( int pfd[2] );

int main(int argc, const char * argv[]) {
    PipeType myPipe;
    pipe(myPipe.pipefd);
    
    //SetNonBlock( myPipe.pipefd[0] );
    
    stCoRoutine_t *recvCo = 0;
    co_create( &recvCo,NULL,pipeRecvTask, &myPipe);
    co_resume( recvCo );
    
    std::thread t = std::thread(pipeSendTask, &myPipe);
    //stCoRoutine_t *sendCo = 0;
    //co_create( &sendCo,NULL,pipeSendTask, &myPipe);
    //co_resume( sendCo );
    
    co_eventloop( co_get_epoll_ct(),0,0 );
    return 0;
}
