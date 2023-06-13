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


#include "kuscia/source/filters/http/kuscia_token_auth/config.h"

#include "envoy/registry/registry.h"

#include "kuscia/source/filters/http/kuscia_token_auth/token_auth_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaTokenAuth {

Http::FilterFactoryCb TokenAuthConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::kuscia_token_auth::v3::TokenAuth& proto_config,
    const std::string&,
    Server::Configuration::FactoryContext&) {
    TokenAuthConfigSharedPtr config = std::make_shared<TokenAuthConfig>(proto_config);
    return [config](Http::FilterChainFactoryCallbacks & callbacks) -> void {
        callbacks.addStreamDecoderFilter(std::make_shared<TokenAuthFilter>(config));
    };
}

Router::RouteSpecificFilterConfigConstSharedPtr TokenAuthConfigFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::kuscia_token_auth::v3::FilterConfigPerRoute&
    proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
    return std::make_shared<FilterConfigPerRoute>(proto_config);
}

REGISTER_FACTORY(TokenAuthConfigFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace KusciaTokenAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
