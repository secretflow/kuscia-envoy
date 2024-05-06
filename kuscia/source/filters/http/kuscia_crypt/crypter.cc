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
#include "fmt/format.h"
#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"
#include "openssl/aes.h"
#include "openssl/crypto.h"
#include "openssl/evp.h"
#include "openssl/rand.h"
#include "source/common/common/assert.h"
#include "source/common/common/base64.h"
#include <algorithm>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaCrypt {

static const std::string AlgorithmAES("AES");
static constexpr uint32_t AESEncryptBlockSize = 1024;

class AESCrypter : public KusciaCrypter {
public:
  AESCrypter(std::string&& secret_key, std::string&& version, std::string&& iv)
      : KusciaCrypter(std::move(secret_key), std::move(version)), ctx_enc_(EVP_CIPHER_CTX_new()),
        ctx_dec_(EVP_CIPHER_CTX_new()), iv_(std::move(iv)), enc_init_(false), dec_init_(false) {}

  ~AESCrypter() {
    if (ctx_enc_) {
      EVP_CIPHER_CTX_free(ctx_enc_);
    }
    if (ctx_dec_) {
      EVP_CIPHER_CTX_free(ctx_dec_);
    }
  }

  static KusciaCrypterSharedPtr createForwardAESCrypter(std::string&& key, std::string&& version,
                                                        Http::RequestHeaderMap& headers) {
    std::string iv(AES_GCM_IV_LENGTH, '\0');
    int rc = RAND_bytes(reinterpret_cast<uint8_t*>(iv.data()), iv.length());
    ASSERT(rc);

    std::string encoded_iv = Base64::encode(iv.data(), iv.length());
    headers.addCopy(KusciaCommon::HeaderKeyEncryptIv, encoded_iv);
    return std::make_shared<AESCrypter>(std::move(key), std::move(version), std::move(iv));
  }

  static KusciaCrypterSharedPtr createReverseAESCrypter(std::string&& key, std::string&& version,
                                                        Http::RequestHeaderMap& headers) {
    auto encoded_iv = headers.get(KusciaCommon::HeaderKeyEncryptIv);
    if (encoded_iv.empty()) {
      return KusciaCrypterSharedPtr();
    }
    std::string iv = Base64::decode(encoded_iv[0]->value().getStringView());
    return std::make_shared<AESCrypter>(std::move(key), std::move(version), std::move(iv));
  }

  bool checkRespEncrypt(Http::ResponseHeaderMap& headers) override {
    headers.addCopy(KusciaCommon::HeaderKeyEncryptVersion, version_);
    return true;
  }

  bool checkRespDecrypt(Http::ResponseHeaderMap& headers) override {
    auto result = headers.get(KusciaCommon::HeaderKeyEncryptVersion);
    if (result.size() != 1 || result[0] == nullptr || result[0]->value().empty()) {
      return false;
    }
    return true;
  }

  bool encrypt(Buffer::Instance& data, bool end_stream, Buffer::Instance& left_data) override {
    if (data.length() > 0) {
      left_data.move(data);
    }
    // Wait for more data
    if (!end_stream && left_data.length() < AESEncryptBlockSize) {
      return true;
    }
    int out_len = 0;
    size_t data_len = left_data.length();
    std::vector<unsigned char> buffer(data_len + EVP_CIPHER_block_size(EVP_aes_128_gcm()));
    // Initialise key and IV
    if (!enc_init_) {
      enc_init_ = true;
      if (1 != EVP_EncryptInit_ex(ctx_enc_, EVP_aes_128_gcm(), nullptr,
                                  reinterpret_cast<const unsigned char*>(secret_key_.c_str()),
                                  reinterpret_cast<const unsigned char*>(iv_.c_str()))) {
        ENVOY_LOG(warn, "Failed to init encrypt context.");
        return false;
      }
    }
    /*
     * Provide the message to be encrypted, and obtain the encrypted output.
     * EVP_EncryptUpdate can be called multiple times if necessary
     */
    if (data_len > 0) {
      if (1 !=
          EVP_EncryptUpdate(ctx_enc_, buffer.data(), &out_len,
                            reinterpret_cast<const unsigned char*>(left_data.linearize(data_len)),
                            data_len)) {
        ENVOY_LOG(warn, "Failed to encrypt data.");
        return false;
      }
    }

    data.add(buffer.data(), out_len);
    left_data.drain(data_len);

    if (end_stream) {
      /*
       * Finalise the encryption. Normally ciphertext bytes may be written at
       * this stage, but this does not occur in GCM mode
       */
      if (1 != EVP_EncryptFinal_ex(ctx_enc_, buffer.data() + out_len, &out_len)) {
        ENVOY_LOG(warn, "Failed to finalize encrypt.");
        return false;
      }
      // Get the tag
      std::vector<unsigned char> tag(AES_GCM_TAG_LENGTH);
      if (1 !=
          EVP_CIPHER_CTX_ctrl(ctx_enc_, EVP_CTRL_GCM_GET_TAG, AES_GCM_TAG_LENGTH, tag.data())) {
        ENVOY_LOG(warn, "Failed to get tag.");
        return false;
      }
      data.add(tag.data(), AES_GCM_TAG_LENGTH);
    }

    return true;
  }

  bool decrypt(Buffer::Instance& data, bool end_stream, Buffer::Instance& left_data) override {
    if (data.length() > 0) {
      left_data.move(data);
    }
    /*
     * 1. If end_stream and length < AES_GCM_TAG_LENGTH, return false, which means corrupted data
     * 2. If not end_stream and length < AESEncryptBlockSize, return true, which means wait for
     * more data
     */
    if (end_stream) {
      if (left_data.length() < AES_GCM_TAG_LENGTH) {
        ENVOY_LOG(warn, "Data corrupted, length < tag.");
        return false;
      }
    } else if (left_data.length() < AESEncryptBlockSize) {
      return true;
    }
    int out_len = 0;
    size_t data_len = left_data.length() - AES_GCM_TAG_LENGTH;
    std::vector<unsigned char> buffer(data_len);
    // Initialise key and IV
    if (!dec_init_) {
      dec_init_ = true;
      if (1 != EVP_DecryptInit_ex(ctx_dec_, EVP_aes_128_gcm(), nullptr,
                                  reinterpret_cast<const unsigned char*>(secret_key_.c_str()),
                                  reinterpret_cast<const unsigned char*>(iv_.c_str()))) {
        ENVOY_LOG(warn, "Failed to init decrypt context.");
        return false;
      }
    }
    /*
     * Provide the message to be decrypted, and obtain the plaintext output.
     * EVP_DecryptUpdate can be called multiple times if necessary
     */
    if (data_len > 0) {
      if (1 !=
          EVP_DecryptUpdate(ctx_dec_, buffer.data(), &out_len,
                            reinterpret_cast<const unsigned char*>(left_data.linearize(data_len)),
                            data_len)) {
        ENVOY_LOG(warn, "Failed to dcrypt data.");
        return false;
      }
      data.add(buffer.data(), out_len);
    }

    left_data.drain(data_len);

    if (end_stream) {
      // Set expected tag value. Works in OpenSSL 1.0.1d and later
      std::vector<unsigned char> tag(AES_GCM_TAG_LENGTH);
      left_data.copyOut(0, AES_GCM_TAG_LENGTH, tag.data());
      left_data.drain(AES_GCM_TAG_LENGTH);
      if (1 !=
          EVP_CIPHER_CTX_ctrl(ctx_dec_, EVP_CTRL_GCM_SET_TAG, AES_GCM_TAG_LENGTH, tag.data())) {
        ENVOY_LOG(warn, "Failed to set tag.");
        return false;
      }
      /*
       * Finalise the decryption. A positive return value indicates success,
       * anything else is a failure - the plaintext is not trustworthy.
       */
      if (1 != EVP_DecryptFinal_ex(ctx_dec_, buffer.data() + out_len, &out_len)) {
        ENVOY_LOG(warn, "Failed to finalize decrypt.");
        return false;
      }
    }
    return true;
  }

private:
  EVP_CIPHER_CTX* ctx_enc_;
  EVP_CIPHER_CTX* ctx_dec_;
  std::string iv_;
  bool enc_init_;
  bool dec_init_;
};

KusciaCrypterSharedPtr KusciaCrypter::createForwardCrypter(const CryptRule& rule,
                                                           Http::RequestHeaderMap& headers) {
  auto encrypt_version = headers.get(KusciaCommon::HeaderKeyEncryptVersion);
  if (!encrypt_version.empty()) {
    return KusciaCrypterSharedPtr();
  }
  headers.addCopy(KusciaCommon::HeaderKeyEncryptVersion, rule.secret_key_version());

  if (rule.algorithm() == AlgorithmAES) {
    return AESCrypter::createForwardAESCrypter(std::string(rule.secret_key()),
                                               std::string(rule.secret_key_version()), headers);
  }
  return KusciaCrypterSharedPtr();
}

KusciaCrypterSharedPtr KusciaCrypter::createReverseCrypter(const CryptRule& rule,
                                                           Http::RequestHeaderMap& headers) {
  std::string encrypt_version;
  auto value = headers.get(KusciaCommon::HeaderKeyEncryptVersion);
  if (value.empty()) {
    return KusciaCrypterSharedPtr();
  }
  encrypt_version = std::string(value[0]->value().getStringView());

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
    return AESCrypter::createReverseAESCrypter(std::move(secret_key), std::move(encrypt_version),
                                               headers);
  }
  return KusciaCrypterSharedPtr();
}

} // namespace KusciaCrypt
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
