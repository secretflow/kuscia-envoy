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


#pragma once

#include <map>
#include <memory>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "kuscia/api/filters/http/kuscia_crypt/v3/crypt.pb.h"
#include "kuscia/source/filters/http/kuscia_crypt/crypter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaCrypt {

class CryptFilterTest;

class KusciaCryptConfig;
using CryptConfigSharedPtr = std::shared_ptr<KusciaCryptConfig>;
using CryptPbConfig = envoy::extensions::filters::http::kuscia_crypt::v3::Crypt;

class CryptFilter : public Envoy::Http::PassThroughFilter,
    public Logger::Loggable<Logger::Id::filter> {
  public:
    explicit CryptFilter(CryptConfigSharedPtr config) :
        config_(config),
        request_id_(),
        peer_origin_source_(),
        enable_resp_encrypt_(false),
        enable_resp_decrypt_(false) {}

    Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                            bool end_stream) override;
    Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

    Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                            bool end_stream) override;
    Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;

  private:
    void createForwardCrypter(Http::RequestHeaderMap& headers, bool end_stream);
    void createReverseCrypter(Http::RequestHeaderMap& headers, bool end_stream);
    void checkRespEncrypt(Http::ResponseHeaderMap& headers, bool end_stream);
    void checkRespDecrypt(Http::ResponseHeaderMap& headers, bool end_stream);

    CryptConfigSharedPtr config_;
    std::string request_id_;
    std::string peer_origin_source_;

    bool enable_resp_encrypt_;
    bool enable_resp_decrypt_;
    KusciaCrypterSharedPtr forward_crypter_;
    KusciaCrypterSharedPtr reverse_crypter_;

    Buffer::OwnedImpl left_data_;

    friend class CryptFilterTest;
};

class KusciaCryptConfig {
  public:
    explicit KusciaCryptConfig(const CryptPbConfig& config);

    const std::string& selfNamespace() const;
    bool forwardEncryption() const;
    bool reverseEncryption() const;
    const CryptRule* getEncryptRule(const std::string& source, const std::string& dest) const;
    const CryptRule* getDecryptRule(const std::string& source, const std::string& dest) const;

  private:
    std::string namespace_;
    std::map<std::pair<std::string, std::string>, CryptRule> encrypt_rules_;
    std::map<std::pair<std::string, std::string>, CryptRule> decrypt_rules_;
};

} // namespace KusciaCrypt
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
