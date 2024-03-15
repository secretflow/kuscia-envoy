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

#include <unordered_map>
#include <memory>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "kuscia/api/filters/http/kuscia_gress/v3/gress.pb.h"

#include "envoy/common/matchers.h"
#include "source/common/common/matchers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaGress {

using GressPbConfig = envoy::extensions::filters::http::kuscia_gress::v3::Gress;
using RewriteHost = envoy::extensions::filters::http::kuscia_gress::v3::Gress_RewriteHostByHeader;
using RewritePolicy = RewriteHost::RewritePolicy;
using PathMatcherConstSharedPtr = std::shared_ptr<const Envoy::Matchers::PathMatcher>;

class RewriteHostConfig {
  public:
    explicit RewriteHostConfig(const RewriteHost& config);

    const std::string& header() const {
        return header_;
    }
    RewritePolicy rewritePolicy() const {
        return rewrite_policy_;
    }
    const std::string& specifiedHost() const {
        return specified_host_;
    }

    const std::vector<PathMatcherConstSharedPtr>& pathMatchers() const {
        return path_matchers_;
    }

  private:
    RewriteHost::RewritePolicy rewrite_policy_;
    std::string header_;
    std::string specified_host_;
    std::vector<PathMatcherConstSharedPtr> path_matchers_;
};

class GressFilterConfig {
  public:
    explicit GressFilterConfig(const GressPbConfig& config);
    const std::string& instance() const {
        return instance_;
    }

    const std::string& selfNamespace() const {
        return self_namespace_;
    }

    bool addOriginSource() const {
        return add_origin_source_;
    }

    int32_t maxLoggingBodySizePerReqeuest() {
        return max_logging_body_size_per_reqeuest_;
    }

    const std::vector<RewriteHostConfig>& rewriteHostConfig() const {
        return rewrite_host_config_;
    }

  private:
    std::string instance_;
    std::string self_namespace_;
    bool        add_origin_source_;
    int32_t     max_logging_body_size_per_reqeuest_;

    std::vector<RewriteHostConfig> rewrite_host_config_;
};

using GressFilterConfigSharedPtr = std::shared_ptr<GressFilterConfig>;


class GressFilter : public Envoy::Http::PassThroughFilter,
    public Logger::Loggable<Logger::Id::filter> {
  public:
    explicit GressFilter(GressFilterConfigSharedPtr config) :
        config_(config),
        host_(),
        request_id_(),
        record_request_body_(false),
        record_response_body_(false) {}

    Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                            bool) override;
    Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

    Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                            bool end_stream) override;
    Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;

  private:
    bool rewriteHost(Http::RequestHeaderMap& headers);
    bool rewriteHost(Http::RequestHeaderMap& headers, const RewriteHostConfig& rh);
    bool recordBody(Buffer::OwnedImpl& body, Buffer::Instance& data, bool end_stream, bool is_req);

    GressFilterConfigSharedPtr config_;
    std::string host_;
    std::string request_id_;

    bool record_request_body_;
    bool record_response_body_;
    Buffer::OwnedImpl req_body_;
    Buffer::OwnedImpl resp_body_;
};

} // namespace KusciaGress
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
