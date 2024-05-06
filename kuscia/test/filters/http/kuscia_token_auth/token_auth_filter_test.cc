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

#include "kuscia/api/filters/http/kuscia_token_auth/v3/token_auth.pb.h"
#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"
#include "kuscia/source/filters/http/kuscia_token_auth/token_auth_filter.h"

#include "kuscia/test/filters/http/test_common/header_checker.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaTokenAuth {
namespace {

using namespace Envoy::Extensions::HttpFilters::KusciaTest;

const std::string Token1("token1");
const std::string Token2("token2");

class TokenAuthFilterTest : public testing::Test {
public:
  TokenAuthFilterTest() : filter_(setupConfig()) {
    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
  }

  TokenAuthConfigSharedPtr setupConfig() {
    TokenAuthPbConfig proto_config;
    auto token_auth = proto_config.mutable_source_token_list()->Add();
    token_auth->set_source("alice");
    token_auth->add_tokens(Token1);
    token_auth->add_tokens(Token2);
    return std::make_shared<TokenAuthConfig>(proto_config);
  }

  TokenAuthFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
};

TEST_F(TokenAuthFilterTest, AuthSuccWithToken1) {
  Http::TestRequestHeaderMapImpl headers{{kKusciaSource, "alice"}, {kKusciaToken, Token1}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, true));
}

TEST_F(TokenAuthFilterTest, AuthSuccWithToken2) {
  Http::TestRequestHeaderMapImpl headers{{kKusciaSource, "alice"}, {kKusciaToken, Token2}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, true));
}

TEST_F(TokenAuthFilterTest, AuthInvlidSource) {
  Http::TestRequestHeaderMapImpl headers{{kKusciaSource, "bob"}, {kKusciaToken, Token2}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, true));
}

TEST_F(TokenAuthFilterTest, AuthInvlidToken) {
  Http::TestRequestHeaderMapImpl headers{{kKusciaSource, "alice"}, {kKusciaToken, "Token3"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, true));
}

} // namespace
} // namespace KusciaTokenAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
