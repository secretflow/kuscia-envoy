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


#include "kuscia/source/filters/http/kuscia_header_decorator/header_decorator_filter.h"

#include "source/common/common/empty_string.h"
#include "source/common/http/utility.h"

#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaHeaderDecorator {

HeaderDecoratorFilter::HeaderDecoratorFilter(const HeaderDecoratorPbConfig& config) {
    for (const auto& source_headers : config.append_headers()) {
        std::vector<std::pair<std::string, std::string>> headers;
        headers.reserve(source_headers.headers_size());
        for (const auto& entry : source_headers.headers()) {
            headers.emplace_back(entry.key(), entry.value());
        }
        append_headers_.emplace(source_headers.source(), headers);
    }
}

Http::FilterHeadersStatus HeaderDecoratorFilter::decodeHeaders(Http::RequestHeaderMap& headers,
        bool) {
    appendHeaders(headers);
    return Http::FilterHeadersStatus::Continue;
}

void HeaderDecoratorFilter::appendHeaders(Http::RequestHeaderMap& headers) const {
    auto source = headers.getByKey(KusciaCommon::HeaderKeyKusciaSource).value_or("");
    auto iter = append_headers_.find(source);
    if (iter != append_headers_.end()) {
        for (const auto& entry : iter->second) {
            headers.addCopy(Http::LowerCaseString(entry.first), entry.second);
        }
    }
}



} // namespace KusciaHeaderDecorator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
