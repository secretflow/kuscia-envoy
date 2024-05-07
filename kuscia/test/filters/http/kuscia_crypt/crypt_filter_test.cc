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

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "source/common/stream_info/stream_info_impl.h"
#include "test/mocks/http/mocks.h"

#include "kuscia/api/filters/http/kuscia_crypt/v3/crypt.pb.h"
#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"
#include "kuscia/source/filters/http/kuscia_crypt/crypt_filter.h"

#include "kuscia/test/filters/http/test_common/header_checker.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaCrypt {

using namespace Envoy::Extensions::HttpFilters::KusciaTest;

const std::string kFirstKey("123456789-123456");
const std::string kFirstKeyVersion("1");
const std::string kSecondKey("987654321-654321");
const std::string kSecondKeyVersion("2");

const std::string kEncryptIv(KusciaCommon::HeaderKeyEncryptIv.get());
const std::string kEncryptVersion(KusciaCommon::HeaderKeyEncryptVersion.get());

class CryptFilterTest : public testing::Test {
public:
  CryptFilterTest() : filter_(setupConfig()), peer_filter_(peerSetupConfig()) {
    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    filter_.setEncoderFilterCallbacks(encoder_callbacks_);
    peer_filter_.setDecoderFilterCallbacks(decoder_callbacks_peer_);
    peer_filter_.setEncoderFilterCallbacks(encoder_callbacks_peer_);
  }

  CryptConfigSharedPtr setupConfig() {
    CryptPbConfig proto_config;
    proto_config.set_self_namespace("alice");

    auto encrypt_config = proto_config.mutable_encrypt_rules()->Add();
    encrypt_config->set_source("alice");
    encrypt_config->set_destination("bob");
    encrypt_config->set_secret_key(kFirstKey);
    encrypt_config->set_secret_key_version(kFirstKeyVersion);
    encrypt_config->set_algorithm("AES");

    return std::make_shared<KusciaCryptConfig>(proto_config);
  }

  CryptConfigSharedPtr peerSetupConfig() {
    CryptPbConfig proto_config;
    proto_config.set_self_namespace("bob");

    auto decrypt_config = proto_config.mutable_decrypt_rules()->Add();
    decrypt_config->set_source("alice");
    decrypt_config->set_destination("bob");
    decrypt_config->set_algorithm("AES");
    decrypt_config->set_secret_key(kFirstKey);
    decrypt_config->set_secret_key_version(kFirstKeyVersion);
    decrypt_config->set_reserve_key(kSecondKey);
    decrypt_config->set_reserve_key_version(kSecondKeyVersion);
    return std::make_shared<KusciaCryptConfig>(proto_config);
  }

  void checkEncryptionParams(bool forward_encrypt, absl::string_view forward_key,
                             bool reverse_encrypt, absl::string_view reverse_key) {
    EXPECT_EQ(forward_encrypt, static_cast<bool>(filter_.forward_crypter_));
    if (filter_.forward_crypter_) {
      EXPECT_EQ(forward_key, filter_.forward_crypter_->secret_key_);
    }
    EXPECT_EQ(reverse_encrypt, static_cast<bool>(peer_filter_.reverse_crypter_));
    if (peer_filter_.reverse_crypter_) {
      EXPECT_EQ(reverse_key, peer_filter_.reverse_crypter_->secret_key_);
    }
  }

  CryptFilter filter_;
  CryptFilter peer_filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_peer_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_peer_;
};

TEST_F(CryptFilterTest, RequestAndResponse) {
  // request
  Http::TestRequestHeaderMapImpl request_headers{{kHost, "hello.bob.svc"}};

  // alice: decodeheader, create forwardcrypter
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  KusciaHeaderChecker::checkRequestHeaders(
      request_headers,
      ExpectHeaders{{kEncryptVersion, kFirstKeyVersion}, {kEncryptIv, "", false}});
  // alice: decodedata, encrypt request body
  std::string data("something plaintext");
  Envoy::Buffer::OwnedImpl request_body(data);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_body, true));
  EXPECT_NE(data, request_body.toString());
  EXPECT_EQ(data.length() + AES_GCM_TAG_LENGTH, request_body.length());

  // bob: decode header, create reverse crypter
  request_headers.addCopy(kKusciaHost, "hello.bob.svc");
  request_headers.addCopy(kOriginSource, "alice");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            peer_filter_.decodeHeaders(request_headers, false));
  checkEncryptionParams(true, kFirstKey, true, kFirstKey);

  // bob: decodedata, decrypt request body
  EXPECT_EQ(Http::FilterDataStatus::Continue, peer_filter_.decodeData(request_body, true));
  EXPECT_EQ(data, request_body.toString()); // after decrypt, data is restored

  // response
  Http::TestResponseHeaderMapImpl response_headers{};
  response_headers.setStatus("200");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            peer_filter_.encodeHeaders(response_headers, false));
  KusciaHeaderChecker::checkResponseHeaders(
      response_headers,
      ExpectHeaders{{kOriginSource, "alice"}, {kEncryptVersion, kFirstKeyVersion}});

  Envoy::Buffer::OwnedImpl response_body(data);
  EXPECT_EQ(Http::FilterDataStatus::Continue, peer_filter_.encodeData(response_body, true));
  EXPECT_NE(data, response_body.toString());
  EXPECT_EQ(data.length() + AES_GCM_TAG_LENGTH, response_body.length());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  checkEncryptionParams(true, kFirstKey, true, kFirstKey);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(response_body, true));
  EXPECT_EQ(data, response_body.toString()); // after decrypt, data is restored
}

TEST_F(CryptFilterTest, BigRequestBody) {
  Http::TestRequestHeaderMapImpl request_headers{{kHost, "hello.bob.svc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  KusciaHeaderChecker::checkRequestHeaders(
      request_headers,
      ExpectHeaders{{kEncryptVersion, kFirstKeyVersion}, {kEncryptIv, "", false}});

  const uint32_t kBodySize = 10 * 4096 + 2000; // just a casual length
  char data[kBodySize];                        // no need to initialize
  Envoy::Buffer::OwnedImpl request_body(data, kBodySize);
  Envoy::Buffer::OwnedImpl encrypted_body;
  srand(time(NULL));

  auto remain_length = kBodySize;
  auto len = rand() % 8192;
  while (remain_length > 8192) {
    Envoy::Buffer::OwnedImpl body(request_body.linearize(len), len);
    request_body.drain(len);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(body, false));
    encrypted_body.add(body);
    remain_length -= len;
    len = rand() % 8192;
  }

  if (remain_length > 0) {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_body, true));
    encrypted_body.add(request_body);
  }
  EXPECT_NE(Envoy::Buffer::OwnedImpl(data, kBodySize).toString(), encrypted_body.toString());
  EXPECT_EQ(kBodySize + AES_GCM_TAG_LENGTH, encrypted_body.length());

  request_headers.addCopy(kKusciaHost, "hello.bob.svc");
  request_headers.addCopy(kOriginSource, "alice");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            peer_filter_.decodeHeaders(request_headers, false));
  checkEncryptionParams(true, kFirstKey, true, kFirstKey);

  remain_length = kBodySize;
  len = rand() % 8192;
  Envoy::Buffer::OwnedImpl decrypted_body;
  while (remain_length > 8192) {
    Envoy::Buffer::OwnedImpl body(encrypted_body.linearize(len), len);
    encrypted_body.drain(len);
    EXPECT_EQ(Http::FilterDataStatus::Continue, peer_filter_.decodeData(body, false));
    decrypted_body.add(body);
    remain_length -= len;
    len = rand() % 8192;
  }
  if (remain_length > 0) {
    EXPECT_EQ(Http::FilterDataStatus::Continue, peer_filter_.decodeData(encrypted_body, true));
    decrypted_body.add(encrypted_body);
  }
  EXPECT_EQ(Envoy::Buffer::OwnedImpl(data, kBodySize).toString(),
            decrypted_body.toString()); // after decrypt, data is restored
}

TEST_F(CryptFilterTest, NoEncrypt) {
  Http::TestRequestHeaderMapImpl request_headers{{kHost, "hello.joke.svc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  KusciaHeaderChecker::checkRequestHeaders(request_headers, ExpectHeaders{{kEncryptVersion, ""}});

  request_headers.addCopy(kOriginSource, "alice");
  request_headers.addCopy(kKusciaHost, "hello.bob.svc");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            peer_filter_.decodeHeaders(request_headers, false));

  checkEncryptionParams(false, "", false, "");
}

TEST_F(CryptFilterTest, UseReverseEnCryptKey) {
  Http::TestRequestHeaderMapImpl request_headers{{kKusciaHost, "hello.bob.svc"},
                                                 {kOriginSource, "alice"},
                                                 {kEncryptVersion, kSecondKeyVersion},
                                                 {kEncryptIv, "1"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            peer_filter_.decodeHeaders(request_headers, false));
  checkEncryptionParams(false, "", true, kSecondKey);
}

} // namespace KusciaCrypt
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
