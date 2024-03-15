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

#include "envoy/http/header_map.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaCommon {

const Http::LowerCaseString HeaderKeyInterConnProtocol("x-interconn-protocol");

const Http::LowerCaseString HeaderKeyBFIAPTPSource("x-ptp-source-node-id");
const Http::LowerCaseString HeaderKeyBFIAScheduleSource("x-node-id");

const Http::LowerCaseString HeaderKeyKusciaSource("Kuscia-Source");
const Http::LowerCaseString HeaderKeyKusciaToken("Kuscia-Token");
const Http::LowerCaseString HeaderKeyKusciaHost("Kuscia-Host");
const Http::LowerCaseString HeaderKeyOriginSource("Kuscia-Origin-Source");


const Http::LowerCaseString HeaderKeyErrorMessage("Kuscia-Error-Message");
const Http::LowerCaseString HeaderKeyFmtError("Kuscia-Error-Formatted");
const Http::LowerCaseString HeaderKeyErrorMessageInternal("Kuscia-Error-Message-Internal");
const Http::LowerCaseString HeaderKeyRecordBody("Kuscia-Record-Body");

const Http::LowerCaseString HeaderKeyEncryptVersion("Kuscia-Encrypt-Version");
const Http::LowerCaseString HeaderKeyEncryptIv("Kuscia-Encrypt-Iv");

class KusciaHeader {
  public:
    static absl::optional<absl::string_view> getSource(const Http::RequestHeaderMap& headers);
};

} // namespace KusciaCommon
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
