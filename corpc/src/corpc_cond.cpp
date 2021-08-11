/*
 * Created by Xianke Liu on 2021/8/11.
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

#include "corpc_cond.h"

using namespace corpc;

Cond::Cond() {
	_cond = co_cond_alloc();
}

Cond::~Cond() {
	co_cond_free(_cond);
}

void Cond::signal() {
	co_cond_signal(_cond);
}

void Cond::broadcast() {
	co_cond_broadcast(_cond);
}

void Cond::wait(int timeout) {
	co_cond_timedwait(_cond, timeout);
}