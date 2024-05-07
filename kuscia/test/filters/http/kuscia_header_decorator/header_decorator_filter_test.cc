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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "source/common/stream_info/stream_info_impl.h"
#include "test/mocks/http/mocks.h"

#include "kuscia/api/filters/http/kuscia_header_decorator/v3/header_decorator.pb.h"
#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"
#include "kuscia/source/filters/http/kuscia_header_decorator/header_decorator_filter.h"

#include "kuscia/test/filters/http/test_common/header_checker.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaHeaderDecorator {
namespace {

using testing::_;
using namespace Envoy::Extensions::HttpFilters::KusciaTest;

class HeaderDecoratorFilterTest : public testing::Test {
public:
  HeaderDecoratorFilterTest() : filter_(setupConfig()) {}

  HeaderDecoratorPbConfig setupConfig() {
    HeaderDecoratorPbConfig proto_config;
    auto source_header = proto_config.mutable_append_headers()->Add();
    source_header->set_source("alice");
    auto header1 = source_header->mutable_headers()->Add();
    header1->set_key("k1");
    header1->set_value("v1");

    auto header2 = source_header->mutable_headers()->Add();
    header2->set_key("k2");
    header2->set_value("v2");

    return proto_config;
  }

  HeaderDecoratorFilter filter_;
};

TEST_F(HeaderDecoratorFilterTest, append_with_empty_header) {
  Http::TestRequestHeaderMapImpl headers{{kKusciaSource, "alice"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, true));
  KusciaHeaderChecker::checkRequestHeaders(headers, ExpectHeaders{{"k1", "v1"}, {"k2", "v2"}});
}

TEST_F(HeaderDecoratorFilterTest, append_with_unempty_header) {
  Http::TestRequestHeaderMapImpl headers{{"k1", "v3"}, {kKusciaSource, "alice"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, true));
  KusciaHeaderChecker::checkRequestHeaders(headers, ExpectHeaders{{"k1", "v3"}, {"k2", "v2"}});
}

TEST_F(HeaderDecoratorFilterTest, umatch_source) {
  Http::TestRequestHeaderMapImpl headers{{kKusciaSource, "bob"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, true));
  KusciaHeaderChecker::checkRequestHeaders(
      headers, ExpectHeaders{{"k1", "v1", false}, {"k2", "v2", false}});
}

} // namespace
} // namespace KusciaHeaderDecorator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
