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

#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaCommon {

constexpr absl::string_view InterConnProtocolBFIA{"bfia"};
constexpr absl::string_view InterConnProtocolKuscia{"kuscia"};

absl::optional<absl::string_view> KusciaHeader::getSource(const Http::RequestHeaderMap& headers) {
  absl::string_view kusciaSource;
  auto source = headers.get(HeaderKeyKusciaSource);
  if (!source.empty()) {
    return source[0]->value().getStringView();
  }
  // BFIA protocol
  auto protocol = headers.get(KusciaCommon::HeaderKeyInterConnProtocol);
  if (!protocol.empty() && std::string(protocol[0]->value().getStringView()) == InterConnProtocolBFIA) {
    auto ptpSource = headers.get(HeaderKeyBFIAPTPSource);
    if (!ptpSource.empty()) {
        return ptpSource[0]->value().getStringView();
    }

    auto scheduleSource = headers.get(HeaderKeyBFIAScheduleSource);
    if (!scheduleSource.empty()) {
        return scheduleSource[0]->value().getStringView();
    }
  }
  return kusciaSource;
}

void adjustContentLength(Http::RequestOrResponseHeaderMap& headers, int64_t delta_length) {
  auto length_header = headers.getContentLengthValue();
  if (!length_header.empty()) {
    int64_t old_length;
    if (absl::SimpleAtoi(length_header, &old_length)) {
      if (old_length > 0 && old_length + delta_length >= 0) {
        headers.setContentLength(old_length + delta_length);
      }
    }
  }
}

} // namespace KusciaCommon
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
