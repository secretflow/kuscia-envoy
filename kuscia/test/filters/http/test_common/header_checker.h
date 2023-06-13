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


#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "test/mocks/http/mocks.h"

#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaTest {

const std::string kHost(":authority");
const std::string kOrginSource(KusciaCommon::HeaderKeyOriginSource.get());
const std::string kKusciaHost(KusciaCommon::HeaderKeyKusciaHost.get());
const std::string kKusciaSource(KusciaCommon::HeaderKeyKusciaSource.get());
const std::string kKusciaToken(KusciaCommon::HeaderKeyKusciaToken.get());
const std::string kOriginSource(KusciaCommon::HeaderKeyOriginSource.get());

struct ExpectHeader {
    absl::string_view key;
    absl::string_view value;
    bool equal = true;
    bool exist = true;
};

using ExpectHeaders = std::vector<ExpectHeader>;

class KusciaHeaderChecker {
  public:
    static void checkRequestHeaders(const Http::TestRequestHeaderMapImpl& headers,
                                    const ExpectHeaders& expects) {
        for (const auto& iter : expects) {
            auto result = headers.getByKey(Http::LowerCaseString(iter.key)).value_or(std::string());
            EXPECT_EQ(result == iter.value, iter.equal);
        }
    }

    static void checkResponseHeaders(const Http::TestRequestHeaderMapImpl& headers, const ExpectHeaders& expects) {
        for (const auto& iter : expects) {
            auto result = headers.get(Http::LowerCaseString(iter.key));
            if (!iter.exist) {
                EXPECT_TRUE(result.size() == 0);
            } else {
                EXPECT_TRUE(result.size() == 1 && result[0] != nullptr);
                EXPECT_EQ(result[0]->value() == iter.value, iter.equal);
            }
        }
    }
};

} // namespace KusciaTest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
