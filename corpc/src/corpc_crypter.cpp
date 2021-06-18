/*
 * Created by Xianke Liu on 2020/10/29.
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

#include "corpc_crypter.h"

using namespace corpc;

SimpleXORCrypter::SimpleXORCrypter(const std::string &key): Crypter(), _key(key) {
    _keyBuf = (uint8_t *)_key.data();
    _keySize = _key.size();
}

void SimpleXORCrypter::encrypt(uint8_t *src, uint8_t *dst, int size) {
    for (int i = 0; i < size; i++) {
        dst[i] = src[i] ^ _key[i%_keySize];
    }
}

void SimpleXORCrypter::decrypt(uint8_t *src, uint8_t *dst, int size) {
    encrypt(src, dst, size);
}

