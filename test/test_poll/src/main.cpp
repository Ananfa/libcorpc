/*
 * Created by Xianke Liu on 2018/3/2.
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

#include "corpc_mutex.h"
#include "corpc_utils.h"
#include "corpc_routine_env.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

using namespace corpc;

static void *test_routine( void *arg )
{
    int fd = 13;
    struct stat _stat;
    struct pollfd fd_in;
    fd_in.fd = fd;
    fd_in.events = POLLIN;

    int ret = poll(&fd_in, 1, 4);

    LOG("poll return %d\n", ret);

    if (!fcntl(fd, F_GETFL)) {
        if (!fstat(fd, &_stat)) {
            if (_stat.st_nlink >= 1) {
                LOG("fd ok\n");
            } else {
                LOG("fd not ok\n");
            }
        }
    } else {
        LOG("fd errno: %d\n", errno);
    }
    
    return NULL;
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    corpc::RoutineEnvironment::startCoroutine(test_routine, NULL);

    RoutineEnvironment::runEventLoop();
    
    return 0;
}
