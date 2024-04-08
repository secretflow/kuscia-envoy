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

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaCommon {

#define KUSCIA_RETURN_IF(expr)                                                                    \
  do {                                                                                            \
    if (expr) {                                                                                   \
      return;                                                                                     \
    }                                                                                             \
  } while (false)

#define KUSCIA_RETURN_RET_IF(expr, ret)                                                           \
  do {                                                                                            \
    if (expr) {                                                                                   \
      return ret;                                                                                 \
    }                                                                                             \
  } while (false)

} // namespace KusciaCommon
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
