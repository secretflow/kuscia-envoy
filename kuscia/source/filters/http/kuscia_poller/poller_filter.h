// Copyright 2024 Ant Group Co., Ltd.
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

#include "kuscia/source/filters/http/kuscia_common/coder.h"
#include "kuscia/source/filters/http/kuscia_poller/common.h"
#include "source/common/common/logger.h"
#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "kuscia/source/filters/http/kuscia_poller/callbacks.h"
#include "envoy/event/timer.h"
#include "envoy/common/time.h"
#include <map>
#include <vector>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaPoller {

class PollerFilter : public Http::PassThroughFilter, public Logger::Loggable<Logger::Id::http> {
public:
    explicit PollerFilter(const PollerConfigPbConfig& config, Upstream::ClusterManager& cluster_manager, TimeSource& time_source);
    ~PollerFilter();

    Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;

    // Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

    Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool end_stream) override;

    Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;

private:
    bool attemptToDecodeMessage(Buffer::Instance& data);

    int32_t forwardMessage(const RequestMessagePb &message, std::string& errmsg);
    int32_t forwardToApplication(Http::AsyncClient& client, Http::RequestMessagePtr& req_msg, const std::string& msg_id, std::string& errmsg);
    int32_t forwardToApiserver(Http::AsyncClient& client, Http::RequestMessagePtr& req_msg, const std::string& msg_id, std::string& errmsg);

    void appendHeaders(Http::RequestHeaderMap& headers);
    void sendHeartbeat();

    bool forward_response_{false};
    std::string conn_id_;
    std::string peer_domain_;
    std::string receiver_service_name_;
    std::string peer_receiver_host_;
    int req_timeout_;
    int rsp_timeout_;
    int heartbeat_interval_;
    Upstream::ClusterManager& cluster_manager_;
    KusciaCommon::Decoder decoder_;

    std::map<std::string, std::vector<std::pair<std::string, std::string>>, std::less<>> append_headers_;

    Http::RequestHeaderMapPtr headers_;

    Event::TimerPtr response_timer_;
    TimeSource& time_source_;
};


} // namespace KusciaPoller
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
