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

#ifndef corpc_crypter_h
#define corpc_crypter_h

#include <string>

namespace corpc {
    class Crypter {
    public:
        Crypter() {}
        virtual ~Crypter() {};
        virtual void encrypt(uint8_t *src, uint8_t *dst, int size) = 0;
        virtual void decrypt(uint8_t *src, uint8_t *dst, int size) = 0;
    };

    class SimpleXORCrypter: public Crypter {
    public:
        SimpleXORCrypter(const std::string &key);
        virtual ~SimpleXORCrypter() {}

        virtual void encrypt(uint8_t *src, uint8_t *dst, int size);
        virtual void decrypt(uint8_t *src, uint8_t *dst, int size);

    private:
        std::string key_;
        uint8_t *keyBuf_;
        size_t keySize_;
    };
}

#endif /* corpc_crypter_h */
