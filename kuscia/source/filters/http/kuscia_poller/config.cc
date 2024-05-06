// Copyright 2024 Ant Group Co., Ltd.
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

#include "kuscia/source/filters/http/kuscia_poller/config.h"

#include "envoy/registry/registry.h"

#include "kuscia/source/filters/http/kuscia_poller/poller_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaPoller {

Http::FilterFactoryCb PollerConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::kuscia_poller::v3::Poller& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {

  return [proto_config, &context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<PollerFilter>(
        proto_config, context.serverFactoryContext().clusterManager(), context.serverFactoryContext().timeSource()));
  };
}

REGISTER_FACTORY(PollerConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace KusciaPoller
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
