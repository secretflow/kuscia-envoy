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
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "kuscia/api/filters/http/kuscia_token_auth/v3/token_auth.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaTokenAuth {

class TokenAuthConfig;
using TokenAuthConfigSharedPtr = std::shared_ptr<TokenAuthConfig>;
using TokenAuthPbConfig = envoy::extensions::filters::http::kuscia_token_auth::v3::TokenAuth;

class TokenAuthFilter : public Http::PassThroughDecoderFilter,
    public Logger::Loggable<Logger::Id::filter>  {
  public:
    explicit TokenAuthFilter(TokenAuthConfigSharedPtr config) :
        config_(std::move(config)) {}

    Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                            bool) override;

  private:
    void sendUnauthorizedResponse();

    TokenAuthConfigSharedPtr config_;
};

class TokenAuthConfig {
  public:
    explicit TokenAuthConfig(const TokenAuthPbConfig& config);

    bool validateSource(absl::string_view source, absl::string_view token) const;

  private:
    std::map<std::string, std::vector<std::string>, std::less<>> source_token_map_;
};

class FilterConfigPerRoute : public Router::RouteSpecificFilterConfig {
  public:
    FilterConfigPerRoute(
        const envoy::extensions::filters::http::kuscia_token_auth::v3::FilterConfigPerRoute&
        config)
        : disabled_(config.disabled()) {}

    bool disabled() const {
        return disabled_;
    }

  private:
    bool disabled_;
};

} // namespace KusciaTokenAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

