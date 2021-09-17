/*
 * Created by Xianke Liu on 2021/9/16.
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

#include "corpc_rand.h"

#include "co_routine.h"
#include <random>
#include <thread>
#include <sys/time.h>

namespace corpc {
    uint64_t randInt()
    {
        static thread_local std::mt19937_64* generator = nullptr;
        if (!generator) {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            std::hash<pid_t> pid_hash;
            uint64_t seed = tv.tv_usec + pid_hash(GetPid());
            generator = new std::mt19937_64(seed);
        }
        return (*generator)();
    }

}