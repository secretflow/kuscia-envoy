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


#include "kuscia/source/filters/http/kuscia_token_auth/token_auth_filter.h"

#include "source/common/common/empty_string.h"
#include "source/common/http/utility.h"

#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaTokenAuth {

constexpr absl::string_view UnauthorizedBodyMessage = "unauthorized.";

using KusciaHeader = Envoy::Extensions::HttpFilters::KusciaCommon::KusciaHeader;

Http::FilterHeadersStatus TokenAuthFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                         bool) {
    // Disable filter per route config if applies
    if (decoder_callbacks_->route() != nullptr) {
        const auto* per_route_config =
            Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfigPerRoute>(decoder_callbacks_);
        if (per_route_config != nullptr && per_route_config->disabled()) {
            return Http::FilterHeadersStatus::Continue;
        }
    }

    auto source = KusciaHeader::getSource(headers).value_or("");
    auto token = headers.getByKey(KusciaCommon::HeaderKeyKusciaToken).value_or("");
    bool is_valid = config_->validateSource(source, token);
    if (!is_valid) {
        ENVOY_LOG(warn, "Check Kuscia Source Token fail, {}: {}, {}: {}",
                  KusciaCommon::HeaderKeyKusciaSource, source,
                  KusciaCommon::HeaderKeyKusciaToken, token);
        sendUnauthorizedResponse();
        return Http::FilterHeadersStatus::StopIteration;
    }

    return Http::FilterHeadersStatus::Continue;
}

void TokenAuthFilter::sendUnauthorizedResponse() {
    decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, UnauthorizedBodyMessage, nullptr,
                                       absl::nullopt, Envoy::EMPTY_STRING);
}

TokenAuthConfig::TokenAuthConfig(const TokenAuthPbConfig& config) {
    for (const auto& source_token : config.source_token_list()) {
        std::vector<std::string> tokens;
        tokens.reserve(source_token.tokens_size());
        for (const auto& token : source_token.tokens()) {
            tokens.emplace_back(token);
        }
        source_token_map_.emplace(source_token.source(), tokens);
    }
}

bool TokenAuthConfig::validateSource(absl::string_view source, absl::string_view token) const {
    static const std::string NoopToken = "noop";

    auto iter = source_token_map_.find(source);
    if (iter == source_token_map_.end()) {
        return false;
    }
    for (const auto& disired_token : iter->second) {
        if (token == disired_token || disired_token == NoopToken) {
            return true;
        }
    }
    return false;
}

} // namespace KusciaTokenAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
