//
//  corpc_utils.cpp
//  corpc
//
//  Created by Xianke Liu on 2018/3/14.
//Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_utils.h"

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <errno.h>
#include <string.h>

int setKeepAlive(int fd, int interval)
{
    int val = 1;
    
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1) {
        printf("setsockopt SO_KEEPALIVE: %s", strerror(errno));
        return -1;
    }
    
#if defined( __APPLE__ )
    val = interval;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &val, sizeof(val)) < 0) {
        printf("setsockopt TCP_KEEPALIVE: %s\n", strerror(errno));
        return -1;
    }
    
#else
    /* Send first probe after `interval' seconds. */
    val = interval;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) < 0) {
        printf("setsockopt TCP_KEEPIDLE: %s\n", strerror(errno));
        return -1;
    }
    
    /* Send next probes after the specified interval. Note that we set the
     * delay as interval / 3, as we send three probes before detecting
     * an error (see the next setsockopt call). */
    val = interval/3;
    if (val == 0) val = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) < 0) {
        printf("setsockopt TCP_KEEPINTVL: %s\n", strerror(errno));
        return -1;
    }
    
    /* Consider the socket in error state after three we send three ACK
     * probes without getting a reply. */
    val = 3;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) < 0) {
        printf("setsockopt TCP_KEEPCNT: %s\n", strerror(errno));
        return -1;
    }
#endif
    
    return 0;
}
