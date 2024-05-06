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

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "kuscia/api/filters/http/kuscia_header_decorator/v3/header_decorator.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaHeaderDecorator {

class HeaderDecoratorConfig;
using HeaderDecoratorPbConfig =
    envoy::extensions::filters::http::kuscia_header_decorator::v3::HeaderDecorator;

class HeaderDecoratorFilter : public Http::PassThroughDecoderFilter,
                              public Logger::Loggable<Logger::Id::filter> {
public:
  explicit HeaderDecoratorFilter(const HeaderDecoratorPbConfig& config);

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;

private:
  void appendHeaders(Http::RequestHeaderMap& headers) const;

  std::map<std::string, std::vector<std::pair<std::string, std::string>>, std::less<>>
      append_headers_;
};

} // namespace KusciaHeaderDecorator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
