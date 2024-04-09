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


#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "kuscia/api/filters/http/kuscia_poller/v3/poller.pb.h"
#include "kuscia/api/filters/http/kuscia_poller/v3/poller.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaPoller {

class PollerConfigFactory : public Extensions::HttpFilters::Common::FactoryBase<envoy::extensions::filters::http::kuscia_poller::v3::Poller> {
  public:
    PollerConfigFactory() : FactoryBase("envoy.filters.http.kuscia_poller") {}

    Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
        const envoy::extensions::filters::http::kuscia_poller::v3::Poller&,
        const std::string&,
        Server::Configuration::FactoryContext&) override;
};

} // namespace KusciaPoller
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
