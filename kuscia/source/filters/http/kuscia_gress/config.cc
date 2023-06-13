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


#include "kuscia/source/filters/http/kuscia_gress/config.h"

#include "envoy/registry/registry.h"

#include "kuscia/source/filters/http/kuscia_gress/gress_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaGress {

Http::FilterFactoryCb GressConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::kuscia_gress::v3::Gress& proto_config,
    const std::string&,
    Server::Configuration::FactoryContext&) {
    return [proto_config](Http::FilterChainFactoryCallbacks & callbacks) -> void {
        callbacks.addStreamFilter(std::make_shared<KusciaGress::GressFilter>(proto_config));
    };
}

REGISTER_FACTORY(GressConfigFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace KusciaGress
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
