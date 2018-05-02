//
//  client.cpp
//  echoUdp
//
//  Created by Xianke Liu on 2018/4/28.
//Copyright © 2018年 Dena. All rights reserved.
//

#include <string>
#include <stdio.h> //printf
#include <string.h> //memset
#include <stdlib.h> //exit(0);
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/poll.h>

#include <google/protobuf/message.h>
#include "foo.pb.h"

#define BUFLEN 512  //Max length of buffer
#define PORT 9999

#define CORPC_MSG_TYPE_UDP_HANDSHAKE_1 -111
#define CORPC_MSG_TYPE_UDP_HANDSHAKE_2 -112
#define CORPC_MSG_TYPE_UDP_HANDSHAKE_3 -113
#define CORPC_MSG_TYPE_UDP_HEARTBEAT -115

#define CORPC_MAX_UDP_MESSAGE_SIZE 540

void die(char *s)
{
    perror(s);
    exit(1);
}

int main(int argc, const char * argv[])
{
    if(argc<3){
        printf("Usage:\n"
               "echoUdpclt [IP] [PORT]\n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    
    struct sockaddr_in si_me, si_other;
    socklen_t slen = sizeof(si_other);
    int s;
    char buf[CORPC_MAX_UDP_MESSAGE_SIZE];
    //char buf[BUFLEN];
    //char message[BUFLEN];
    
    if ( (s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        die("socket");
    }
    
    // zero out the structure
    memset((char *) &si_me, 0, sizeof(si_me));
    
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(PORT);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int reuse = 1;
    setsockopt(s , SOL_SOCKET, SO_REUSEADDR, &reuse,sizeof(reuse));
    setsockopt(s , SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
    
    //bind socket to port
    if( bind(s , (struct sockaddr*)&si_me, sizeof(si_me) ) == -1)
    {
        die("bind");
    }
    
    memset((char *) &si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(port);
    int nIP = inet_addr(ip.c_str());
    si_other.sin_addr.s_addr = nIP;
    
    if(connect(s , (struct sockaddr * ) &si_other, slen) == -1)
    {
        die("connect");
    }
    
    struct pollfd fd;
    int ret;
    
    fd.fd = s;
    fd.events = POLLIN;
    
    // 准备握手消息
    char handshake1msg[8];
    char handshake3msg[8];
    char heartbeatmsg[8];
    
    *(uint32_t *)handshake1msg = htonl(0);
    *(uint32_t *)(handshake1msg + 4) = htonl(CORPC_MSG_TYPE_UDP_HANDSHAKE_1);
    
    *(uint32_t *)handshake3msg = htonl(0);
    *(uint32_t *)(handshake3msg + 4) = htonl(CORPC_MSG_TYPE_UDP_HANDSHAKE_3);
    
    *(uint32_t *)heartbeatmsg = htonl(0);
    *(uint32_t *)(heartbeatmsg + 4) = htonl(CORPC_MSG_TYPE_UDP_HEARTBEAT);
    
    // 握手阶段一：发送handshake1消息，然后等待handshake2消息到来，超时未到则重发handshake1消息
    while (true) {
        // 阶段一：发送handshake1消息
        if (write(s, handshake1msg, 8) != 8) {
            die("can't send handshake1");
        }
        
        // 阶段二：接收handshake2消息
        ret = poll(&fd, 1, 1000); // 1 second for timeout
        switch (ret) {
            case -1:
                close(s);
                die("can't recv handshake2");
            case 0:
                continue; // 回阶段一
            default: {
                ret = (int)read(s, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
                if (ret != 8) {
                    close(s);
                    die("recv wrong size of handshake2");
                }
                
                int32_t msgtype = *(int32_t *)(buf + 4);
                msgtype = ntohl(msgtype);
                
                if (msgtype != CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                    close(s);
                    die("not handshake2 type");
                }
                
                break;
            }
        }
        
        // 阶段三：发hankshake3消息
        if (write(s, handshake3msg, 8) != 8) {
            die("can't send handshake3");
        }
        
        break;
    }
    
    // 开始定时心跳
    while(true)
    {
        ret = poll(&fd, 1, 1000); // 1 second for timeout
        switch (ret) {
            case -1:
                close(s);
                die("can't recv heartbeat");
            case 0:
                break;
            default: {
                ret = (int)read(s, buf, CORPC_MAX_UDP_MESSAGE_SIZE);
                if (ret != 8) {
                    close(s);
                    die("recv wrong size of heartbeat");
                }
                
                int32_t msgtype = *(int32_t *)(buf + 4);
                msgtype = ntohl(msgtype);
                
                if (msgtype != CORPC_MSG_TYPE_UDP_HEARTBEAT) {
                    close(s);
                    die("not heartbeat type");
                }
                
                printf("recv heartbeat");
                
                break;
            }
        }
        
        // 发送heartbeat
        if (write(s, heartbeatmsg, 8) != 8) {
            die("can't send heartbeat");
        }
        printf("send heartbeat");
    }
    
    close(s);
    return 0;
}

