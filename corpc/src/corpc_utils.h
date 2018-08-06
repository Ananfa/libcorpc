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

#include <sys/poll.h>

int setKeepAlive(int fd, int interval);

inline void msleep(int msec)
{
    struct pollfd pf = { 0 };
    pf.fd = -1;
    poll( &pf,1,msec );
}

void debuglog(const char *format, ...);
void infolog(const char *format, ...);
void warnlog(const char *format, ...);
void errlog(const char *format, ...);

#ifdef DEBUG
#  define DEBUG_LOG(format, ...) debuglog(format, ## __VA_ARGS__)
#  define LOG(format, ...) infolog(format, ## __VA_ARGS__)
#else
#  define DEBUG_LOG(...)
#  define LOG(...)
#endif
#define WARN_LOG(format, ...) warnlog(format, ## __VA_ARGS__)
#define ERROR_LOG(format, ...) errlog(format, ## __VA_ARGS__)

#endif /* corpc_utils_h */
