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
#include "source/common/stream_info/stream_info_impl.h"

#include "kuscia/source/filters/http/kuscia_gress/gress_filter.h"
#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"
#include "kuscia/api/filters/http/kuscia_gress/v3/gress.pb.h"

#include "kuscia/test/filters/http/test_common/header_checker.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaGress {
namespace {

using namespace Envoy::Extensions::HttpFilters::KusciaTest;

class GressFilterTest : public testing::Test {
  public:
    GressFilterTest() : filter_(setupConfig()), config_(setupConfig()) {
        filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    }

    GressFilterConfigSharedPtr setupConfig() {
        GressPbConfig proto_config;
        proto_config.set_instance("foo");
        proto_config.set_self_namespace("alice");
        proto_config.set_add_origin_source(true);
        proto_config.set_max_logging_body_size_per_reqeuest(5);
        auto rh = proto_config.add_rewrite_host_config();
        rh->set_header("kuscia-Host");
        rh->set_rewrite_policy(RewriteHost::RewriteHostWithHeader);
        return GressFilterConfigSharedPtr(new GressFilterConfig(proto_config));
    }

    void enableRecordBody () {
        std::string kRecordBody(KusciaCommon::HeaderKeyRecordBody.get());
        Http::TestRequestHeaderMapImpl headers{{kRecordBody, "true"}};
        EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, false));
    }

    GressFilter filter_;
    GressFilterConfigSharedPtr config_;
    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
};

TEST_F(GressFilterTest, EmptyHost) {
    Http::TestRequestHeaderMapImpl headers;
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, true));
    KusciaHeaderChecker::checkRequestHeaders(headers,
    ExpectHeaders{{kHost, ""},
        {kOrginSource, config_->selfNamespace()}});
}

TEST_F(GressFilterTest, OtherHost) {
    Http::TestRequestHeaderMapImpl headers{{kHost, "baidu.com"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, true));
    KusciaHeaderChecker::checkRequestHeaders(headers,
    ExpectHeaders{{kHost, "baidu.com"},
        {kOrginSource, config_->selfNamespace()}});
}

TEST_F(GressFilterTest, RewriteHost) {
    std::string source(KusciaCommon::HeaderKeyKusciaHost.get());
    std::string host = "baidu.com";
    Http::TestRequestHeaderMapImpl headers{{source, host}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, true));
    EXPECT_EQ("baidu.com", headers.getHostValue());
}

TEST_F(GressFilterTest, RecordBody) {
    enableRecordBody();

    Event::SimulatedTimeSystem test_time;
    StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time.timeSystem(), nullptr);
    EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info));
    EXPECT_EQ(0, stream_info.dynamicMetadata().filter_metadata_size());

    std::string new_data("he");
    Envoy::Buffer::OwnedImpl request_body(new_data);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_body, false));
    EXPECT_EQ(0, stream_info.dynamicMetadata().filter_metadata_size());

    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_body, true));
    EXPECT_EQ(1, stream_info.dynamicMetadata().filter_metadata_size());
    EXPECT_EQ("hehe", stream_info.dynamicMetadata()
              .filter_metadata()
              .at("envoy.kuscia")
              .fields()
              .at("request_body")
              .string_value());
}

TEST_F(GressFilterTest, RecordBodySizeExceed) {
    enableRecordBody();

    Event::SimulatedTimeSystem test_time;
    StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time.timeSystem(), nullptr);
    EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info));
    EXPECT_EQ(0, stream_info.dynamicMetadata().filter_metadata_size());

    std::string new_data("hee");
    Envoy::Buffer::OwnedImpl request_body(new_data);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_body, false));
    EXPECT_EQ(0, stream_info.dynamicMetadata().filter_metadata_size());

    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_body, true));
    EXPECT_EQ(0, stream_info.dynamicMetadata().filter_metadata_size());
}

} // namespace
} // namespace KusciaGress
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

