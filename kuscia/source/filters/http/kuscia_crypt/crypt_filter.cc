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


#include "kuscia/source/filters/http/kuscia_crypt/crypt_filter.h"

#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"
#include "kuscia/source/filters/http/kuscia_common/common.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaCrypt {

static std::string getNamespaceFromHost(absl::string_view host) {
    std::vector<absl::string_view> fields = absl::StrSplit(host, ".");
    for (std::size_t i = 0; i < fields.size(); i++) {
        if (fields[i] == "svc" && i > 0) {
            return std::string(fields[i - 1]);
        }
    }
    return "";
}

Http::FilterHeadersStatus CryptFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                     bool end_stream) {
    request_id_ = std::string(headers.getRequestIdValue());

    // create encpyter for internal to external request
    if (config_->forwardEncryption()) {
        createForwardCrypter(headers, end_stream);
    }

    // create decrpter for external to internal request
    if (config_->reverseEncryption()) {
        createReverseCrypter(headers, end_stream);
    }

    return  Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CryptFilter::decodeData(Buffer::Instance& data, bool end_stream) {
    // decrypt external to internal request body
    if (reverse_crypter_) {
        reverse_crypter_->decrypt(data, end_stream, left_data_);
        ENVOY_LOG(debug, "decrypt request of {}, decrypted length: {}, remain length{}.",
                  request_id_, data.length(), left_data_.length());
    }

    // encrpt request body from internal to external
    if (forward_crypter_) {
        forward_crypter_->encrypt(data, end_stream, left_data_);
        ENVOY_LOG(debug, "encrypt request of {}, encrypted length: {}, remain length{}.",
                  request_id_, data.length(), left_data_.length());

    }
    return Http::FilterDataStatus::Continue;
}

Http::FilterHeadersStatus CryptFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                     bool end_stream) {
    // resp_decrypter use same key with req_encrypter
    if (config_->forwardEncryption()) {
        checkRespDecrypt(headers, end_stream);
    }

    if (config_->reverseEncryption()) {
        checkRespEncrypt(headers, end_stream);
    }

    return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CryptFilter::encodeData(Buffer::Instance& data, bool end_stream) {
    // decrypt response from external to internal
    if (forward_crypter_ && enable_resp_decrypt_) {
        forward_crypter_->decrypt(data, end_stream, left_data_);
        ENVOY_LOG(debug, "decrypt response of {}, decrypted length: {}, remain length{}.",
                  request_id_, data.length(), left_data_.length());
    }

    // encrypt response from internal to external
    if (reverse_crypter_ && enable_resp_encrypt_) {
        reverse_crypter_->encrypt(data, end_stream, left_data_);
        ENVOY_LOG(debug, "encrypt response of {}, encrypted length: {}, remain length{}.",
                  request_id_, data.length(), left_data_.length());
    }
    return Http::FilterDataStatus::Continue;
}

void CryptFilter::createForwardCrypter(Http::RequestHeaderMap& headers,
                                       bool) {
    std::string source =
        std::string(headers.getByKey(KusciaCommon::HeaderKeyOriginSource).value_or(config_->selfNamespace()));
    auto host = headers.getHostValue();
    std::string dest  = getNamespaceFromHost(host);
    const auto* rule = config_->getEncryptRule(source, dest);
    KUSCIA_RETURN_IF(rule == nullptr);
    forward_crypter_ = KusciaCrypter::createForwardCrypter(*rule, headers);
    if (forward_crypter_ == nullptr) {
        ENVOY_LOG(warn, "create forward encrypter failed. source{}, Dest{}", source, dest);
        return;
    }
}

void CryptFilter::createReverseCrypter(Http::RequestHeaderMap& headers,
                                       bool) {
    // get Rule
    auto peer_host = headers.getByKey(KusciaCommon::HeaderKeyKusciaHost).value_or(std::string());
    std::string dest = getNamespaceFromHost(peer_host);
    std::string source =
        std::string(headers.getByKey(KusciaCommon::HeaderKeyOriginSource).value_or(std::string()));
    KUSCIA_RETURN_IF(source.empty() || dest.empty());
    const auto* rule = config_->getDecryptRule(source, dest);
    KUSCIA_RETURN_IF(rule == nullptr);

    // reverse_crypter_ need to be create even if end_stream == true, cause that req_decrypter is
    // also resp_encrypter
    reverse_crypter_ = KusciaCrypter::createReverseCrypter(*rule, headers);
    if (reverse_crypter_ == nullptr) {
        ENVOY_LOG(warn, "create reverse crypter failed. Source{}, Dest{}.", source, dest);
        return;
    }

    peer_origin_source_ = std::move(source);
}

void CryptFilter::checkRespEncrypt(Http::ResponseHeaderMap& headers, bool end_stream) {
    enable_resp_encrypt_ = false;

    KUSCIA_RETURN_IF(end_stream);
    KUSCIA_RETURN_IF(!reverse_crypter_ || !reverse_crypter_->checkRespEncrypt(headers));

    headers.addCopy(KusciaCommon::HeaderKeyOriginSource, peer_origin_source_);

    enable_resp_encrypt_ = true;
}

void CryptFilter::checkRespDecrypt(Http::ResponseHeaderMap& headers, bool end_stream) {
    enable_resp_decrypt_ = false;

    // check origin namespace
    auto origin_source = headers.get(KusciaCommon::HeaderKeyOriginSource);
    KUSCIA_RETURN_IF(origin_source.size() != 1  || origin_source[0] == nullptr ||
                     origin_source[0]->value() != config_->selfNamespace());

    KUSCIA_RETURN_IF(end_stream);
    KUSCIA_RETURN_IF(!forward_crypter_ || !forward_crypter_->checkRespDecrypt(headers));

    enable_resp_decrypt_ = true;
}

KusciaCryptConfig::KusciaCryptConfig(const CryptPbConfig& config) {
    namespace_ = config.self_namespace();
    for (const auto& rule : config.encrypt_rules()) {
        encrypt_rules_.emplace(std::make_pair(rule.source(), rule.destination()), rule);
    }
    for (const auto& rule : config.decrypt_rules()) {
        decrypt_rules_.emplace(std::make_pair(rule.source(), rule.destination()), rule);
    }
}

const std::string& KusciaCryptConfig::selfNamespace() const {
    return namespace_;
}

bool KusciaCryptConfig::forwardEncryption() const {
    return !encrypt_rules_.empty();
}

bool KusciaCryptConfig::reverseEncryption() const {
    return !decrypt_rules_.empty();
}

const CryptRule* KusciaCryptConfig::getEncryptRule(const std::string& source,
                                                   const std::string& dest) const {
    auto iter = encrypt_rules_.find(std::make_pair(source, dest));
    if (iter == encrypt_rules_.end()) {
        return nullptr;
    }
    return &(iter->second);
}

const CryptRule* KusciaCryptConfig::getDecryptRule(const std::string& source,
                                                   const std::string& dest) const {
    auto iter = decrypt_rules_.find(std::make_pair(source, dest));
    if (iter == decrypt_rules_.end()) {
        return nullptr;
    }
    return &(iter->second);
}

} // namespace KusciaCrypt
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

