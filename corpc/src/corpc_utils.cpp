/*
 * Created by Xianke Liu on 2018/3/14.
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

#include "corpc_utils.h"

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <string>

char* _convert(unsigned int num, int base) {
    static char representation[]= "0123456789ABCDEF";
    static char buffer[50];
    char* ptr;
    
    ptr = &buffer[49];
    *ptr = '\0';
    
    do {
        *--ptr = representation[num%base];
        num /= base;
    } while (num != 0);
    
    return ptr;
}

std::string g_corpc_log_path;
FILE* _getLogFile(uint32_t today) {
    extern char *__progname;
    static __thread uint32_t date(0);
    static __thread FILE* logFile(nullptr);
    static __thread char* exeName(nullptr);
    
    if (g_corpc_log_path.empty()) {
        return stdout;
    } else {
        if (today == date) {
            assert(logFile != nullptr);
            return logFile;
        }
        
        date = today;
        
        if (logFile) {
            fclose(logFile);
            logFile = nullptr;
        }
        
        if (exeName == nullptr) {
            exeName = __progname;
        }
        
        char logFileName[200];
        snprintf(logFileName, 200, "%s%s_%d.log", g_corpc_log_path.c_str(), exeName, today);
        
        logFile = fopen(logFileName, "a");
        
        return logFile;
    }
}

void _print_time(FILE* fd, struct tm& tm_now, uint32_t mseconds) {
    char buffer[26];
    strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", &tm_now);
    fputs(buffer, fd);
    
    snprintf(buffer, 26, ".%03u", mseconds);
    fputs(buffer, fd);
}

namespace corpc {
    
    int setKeepAlive(int fd, int interval)
    {
        int val = 1;
        
        if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1) {
            ERROR_LOG("setsockopt SO_KEEPALIVE: %s", strerror(errno));
            return -1;
        }
        
#if defined( __APPLE__ )
        val = interval;
        if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &val, sizeof(val)) < 0) {
            ERROR_LOG("setsockopt TCP_KEEPALIVE: %s\n", strerror(errno));
            return -1;
        }
        
#else
        /* Send first probe after `interval' seconds. */
        val = interval;
        if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) < 0) {
            ERROR_LOG("setsockopt TCP_KEEPIDLE: %s\n", strerror(errno));
            return -1;
        }
        
        /* Send next probes after the specified interval. Note that we set the
         * delay as interval / 3, as we send three probes before detecting
         * an error (see the next setsockopt call). */
        val = interval/3;
        if (val == 0) val = 1;
        if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) < 0) {
            ERROR_LOG("setsockopt TCP_KEEPINTVL: %s\n", strerror(errno));
            return -1;
        }
        
        /* Consider the socket in error state after three we send three ACK
         * probes without getting a reply. */
        val = 3;
        if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) < 0) {
            ERROR_LOG("setsockopt TCP_KEEPCNT: %s\n", strerror(errno));
            return -1;
        }
#endif
        
        return 0;
    }

    void setLogPath(const char *path)
    {
        if (access(path, F_OK) != 0)
        {
            printf("Error: Log path not exist");
        } else {
            g_corpc_log_path = path;
            if (g_corpc_log_path[g_corpc_log_path.length()-1] != '/') {
                g_corpc_log_path += '/';
            }
        }
    }
    
    void debuglog(const char *format, ...)
    {
        va_list arg;
        va_start(arg, format);
        
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        struct tm tm_now;
        localtime_r(&tv.tv_sec, &tm_now);
        
        uint32_t mseconds = tv.tv_usec / 1000;
        
        uint32_t today = (tm_now.tm_year+1900)*10000+(tm_now.tm_mon+1)*100+tm_now.tm_mday;
        
        FILE *fd = _getLogFile(today);
        
        _print_time(fd, tm_now, mseconds);
        fputs(" [DEBUG] : ", fd);
        
        vfprintf(fd, format, arg);
        
        fflush(fd);
        
        va_end(arg);
    }
    
    void infolog(const char *format, ...)
    {
        va_list arg;
        va_start(arg, format);
        
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        struct tm tm_now;
        localtime_r(&tv.tv_sec, &tm_now);
        
        uint32_t mseconds = tv.tv_usec / 1000;
        
        uint32_t today = (tm_now.tm_year+1900)*10000+(tm_now.tm_mon+1)*100+tm_now.tm_mday;
        
        FILE *fd = _getLogFile(today);
        
        _print_time(fd, tm_now, mseconds);
        fputs(" [INFO]  : ", fd);
        
        vfprintf(fd, format, arg);
        
        fflush(fd);
        
        va_end(arg);
    }
    
    void warnlog(const char *format, ...)
    {
        va_list arg;
        va_start(arg, format);
        
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        struct tm tm_now;
        localtime_r(&tv.tv_sec, &tm_now);
        
        uint32_t mseconds = tv.tv_usec / 1000;
        
        uint32_t today = (tm_now.tm_year+1900)*10000+(tm_now.tm_mon+1)*100+tm_now.tm_mday;
        
        FILE *fd = _getLogFile(today);
        
        _print_time(fd, tm_now, mseconds);
        fputs(" [WARN]  : ", fd);
        
        vfprintf(fd, format, arg);
        
        fflush(fd);
        
        va_end(arg);
    }
    
    void errlog(const char *format, ...)
    {
        va_list arg;
        va_start(arg, format);
        
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        struct tm tm_now;
        localtime_r(&tv.tv_sec, &tm_now);
        
        uint32_t mseconds = tv.tv_usec / 1000;
        
        uint32_t today = (tm_now.tm_year+1900)*10000+(tm_now.tm_mon+1)*100+tm_now.tm_mday;
        
        FILE *fd = _getLogFile(today);
        
        _print_time(fd, tm_now, mseconds);
        fputs(" [ERROR] : ", fd);
        
        vfprintf(fd, format, arg);
        
        fflush(fd);
        
        va_end(arg);
    }

    void fatallog(const char *format, ...)
    {
        va_list arg;
        va_start(arg, format);
        
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        struct tm tm_now;
        localtime_r(&tv.tv_sec, &tm_now);
        
        uint32_t mseconds = tv.tv_usec / 1000;
        
        uint32_t today = (tm_now.tm_year+1900)*10000+(tm_now.tm_mon+1)*100+tm_now.tm_mday;
        
        FILE *fd = _getLogFile(today);
        
        _print_time(fd, tm_now, mseconds);
        fputs(" [FATAL] : ", fd);
        
        vfprintf(fd, format, arg);
        
        fflush(fd);
        
        va_end(arg);
    }

}
