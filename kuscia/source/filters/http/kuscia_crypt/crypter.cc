// Copyright 2023 Ant Group Co., Ltd.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//   http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include "kuscia/source/filters/http/kuscia_crypt/crypter.h"

#include <algorithm>

#include "fmt/format.h"
#include "openssl/aes.h"
#include "openssl/crypto.h"
#include "openssl/evp.h"
#include "openssl/rand.h"

#include "source/common/common/base64.h"
#include "source/common/common/assert.h"

#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaCrypt {

static const std::string AlgorithmAES("AES");
static constexpr uint32_t AESEncryptBlockSize = 1024;


class AESCrypter : public KusciaCrypter {
  public:
    AESCrypter(const std::string& secret_key, const std::string& version, const std::string& iv) :
        KusciaCrypter(secret_key, version),
        iv_(iv) {}

    AESCrypter(std::string&& secret_key, std::string&& version, std::string&& iv):
        KusciaCrypter(std::move(secret_key), std::move(version)),
        iv_(std::move(iv)) {}

    virtual ~AESCrypter() {}

    static KusciaCrypterSharedPtr createForwardAESCrypter(std::string&& key, std::string&& version,
                                                          Http::RequestHeaderMap& headers);

    static KusciaCrypterSharedPtr createReverseAESCrypter(std::string&& key, std::string&& version,
                                                          Http::RequestHeaderMap& headers);

    bool encrypt(Buffer::Instance& data, bool end_stream, Buffer::Instance& left_data) override;
    bool decrypt(Buffer::Instance& data, bool end_stream, Buffer::Instance& left_data) override;

    bool checkRespEncrypt(Http::ResponseHeaderMap& headers) override;
    bool checkRespDecrypt(Http::ResponseHeaderMap& headers) override;

  private:
    bool doCrypt(Buffer::Instance& data, bool end_stream, Buffer::Instance& left_data);

    const std::string iv_;
};

KusciaCrypterSharedPtr KusciaCrypter::createForwardCrypter(const CryptRule& rule,
                                                           Http::RequestHeaderMap& headers) {
    auto encrypt_version =
        headers.getByKey(KusciaCommon::HeaderKeyEncryptVersion).value_or(std::string());
    if (!encrypt_version.empty()) {
        return KusciaCrypterSharedPtr();
    }
    headers.addCopy(KusciaCommon::HeaderKeyEncryptVersion, rule.secret_key_version());

    if (rule.algorithm() == AlgorithmAES) {
        return AESCrypter::createForwardAESCrypter(std::string(rule.secret_key()),
                                                   std::string(rule.secret_key_version()),
                                                   headers);
    }
    return KusciaCrypterSharedPtr();
}

KusciaCrypterSharedPtr KusciaCrypter::createReverseCrypter(const CryptRule& rule,
                                                           Http::RequestHeaderMap& headers) {
    std::string encrypt_version =
        std::string(headers.getByKey(KusciaCommon::HeaderKeyEncryptVersion).value_or(""));
    if (encrypt_version.empty()) {
        return KusciaCrypterSharedPtr();
    }

    std::string secret_key;
    if (encrypt_version == rule.secret_key_version()) {
        secret_key = rule.secret_key();
    } else if (encrypt_version == rule.reserve_key_version()) {
        secret_key = rule.reserve_key();
    } else {
        ENVOY_LOG(warn, "unknown secret key version {}", encrypt_version);
        return KusciaCrypterSharedPtr();
    }

    if (rule.algorithm() == AlgorithmAES) {
        return AESCrypter::createReverseAESCrypter(std::move(secret_key), std::move(encrypt_version), headers);
    }
    return KusciaCrypterSharedPtr();
}

KusciaCrypterSharedPtr AESCrypter::createForwardAESCrypter(std::string&& key, std::string&& version,
                                                           Http::RequestHeaderMap& headers) {
    std::string iv(AES_BLOCK_SIZE, '\0');
    int rc = RAND_bytes(reinterpret_cast<uint8_t*>(iv.data()), iv.length());
    ASSERT(rc);

    std::string encoded_iv = Base64::encode(iv.data(), iv.length());
    headers.addCopy(KusciaCommon::HeaderKeyEncryptIv, encoded_iv);
    return std::make_shared<AESCrypter>(std::move(key), std::move(version), std::move(iv));
}

KusciaCrypterSharedPtr AESCrypter::createReverseAESCrypter(std::string&& key, std::string&& version,
                                                           Http::RequestHeaderMap& headers) {
    auto encoded_iv = headers.getByKey(KusciaCommon::HeaderKeyEncryptIv);
    if (!encoded_iv.has_value()) {
        return KusciaCrypterSharedPtr();
    }
    std::string iv = Base64::decode(encoded_iv.value());
    return std::make_shared<AESCrypter>(std::move(key), std::move(version), std::move(iv));
}

bool AESCrypter::encrypt(Buffer::Instance& data, bool end_stream, Buffer::Instance& left_data) {
    return doCrypt(data, end_stream, left_data);
}

bool AESCrypter::decrypt(Buffer::Instance& data, bool end_stream, Buffer::Instance& left_data) {
    return doCrypt(data, end_stream, left_data);
}

bool AESCrypter::doCrypt(Buffer::Instance& data, bool end_stream, Buffer::Instance& left_data) {
    AES_KEY aes_key;
    if (AES_set_encrypt_key(reinterpret_cast<const unsigned char*>(secret_key_.data()),
                            128, &aes_key) != 0) {
        return false;
    }
    if (left_data.length() > 0) {
        data.prepend(left_data);  // left_data is automatically drained after prepend
    }
    auto remain_length = data.length();
    auto output = std::make_unique<unsigned char[]>(AESEncryptBlockSize);
    auto crypt = [&](uint32_t len) {
        // iv is changed after encrypt, so initialize it every time
        unsigned char iv[AES_BLOCK_SIZE] {};
        std::memcpy(iv, iv_.data(), std::min(AES_BLOCK_SIZE, int(iv_.length())));
        int num = 0;

        // encrypts and decrypts are the same with OFB mode
        AES_ofb128_encrypt(reinterpret_cast<const unsigned char*>(data.linearize(len)),
                           output.get(), len, &aes_key, iv, &num);
        data.drain(len);
        data.add(output.get(), len);
        return len;
    };

    while (remain_length >= AESEncryptBlockSize) {
        remain_length -= crypt(AESEncryptBlockSize);
    }

    if (remain_length > 0) {
        if (end_stream) {
            crypt(remain_length);
        } else {
            left_data.add(data.linearize(remain_length), remain_length);
            data.drain(remain_length);
        }
    }
    return true;
}

bool AESCrypter::checkRespEncrypt(Http::ResponseHeaderMap& headers) {
    headers.addCopy(KusciaCommon::HeaderKeyEncryptVersion, version_);
    return true;
}

bool AESCrypter::checkRespDecrypt(Http::ResponseHeaderMap& headers) {
    auto result = headers.get(KusciaCommon::HeaderKeyEncryptVersion);
    if (result.size() != 1 || result[0] == nullptr || result[0]->value().empty()) {
        return false;
    }
    return true;
}

} // namespace KusciaCrypt
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
