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

#ifndef corpc_utils_h
#define corpc_utils_h

#include <unistd.h>
#include <sys/poll.h>
#include <sys/time.h>

inline void msleep(int msec)
{
    struct pollfd pf = { 0 };
    pf.fd = -1;
    poll( &pf,1,msec );
}

inline uint64_t mtime()
{
    static struct timeval now = { 0 };
    gettimeofday( &now,NULL );
    uint64_t nowms = now.tv_sec;
    nowms *= 1000;
    nowms += now.tv_usec / 1000;
    return nowms;
}

namespace corpc {
    int setKeepAlive(int fd, int interval);

    void setLogPath(const char *path);
    void debuglog(const char *format, ...);
    void infolog(const char *format, ...);
    void warnlog(const char *format, ...);
    void errlog(const char *format, ...);
}

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO 1
#define LOG_LEVEL_WARN 2

#if LOG_LEVEL == LOG_LEVEL_DEBUG
#  define DEBUG_LOG(format, ...) corpc::debuglog(format, ## __VA_ARGS__)
#  define LOG(format, ...) corpc::infolog(format, ## __VA_ARGS__)
#  define WARN_LOG(format, ...) corpc::warnlog(format, ## __VA_ARGS__)
#elif LOG_LEVEL == LOG_LEVEL_INFO
#  define DEBUG_LOG(...)
#  define LOG(format, ...) corpc::infolog(format, ## __VA_ARGS__)
#  define WARN_LOG(format, ...) corpc::warnlog(format, ## __VA_ARGS__)
#elif LOG_LEVEL == LOG_LEVEL_WARN
#  define DEBUG_LOG(...)
#  define LOG(...)
#  define WARN_LOG(format, ...) corpc::warnlog(format, ## __VA_ARGS__)
#else
#  define DEBUG_LOG(...)
#  define LOG(...)
#  define WARN_LOG(...)
#endif
#define ERROR_LOG(format, ...) corpc::errlog(format, ## __VA_ARGS__)

#endif /* corpc_utils_h */
