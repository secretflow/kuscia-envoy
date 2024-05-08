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

#include "kuscia/source/filters/http/kuscia_poller/poller_filter.h"
#include "kuscia/source/filters/http/kuscia_poller/callbacks.h"
#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "poller_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaPoller {

bool starts_with(absl::string_view str, absl::string_view prefix) {
    return str.size() >= prefix.size() && str.substr(0, prefix.size()) == prefix;
}

// isPollRequest checks if a host string matches the format of the three part domain name "receiver.*.svc and a path string matches the format "/poll*"
bool isPollRequest(absl::string_view host, absl::string_view path, const std::string expected_service_name, std::string &domain_id) {
    if (host.size() < 13) { // "receiver..svc" is 13 characters long
        return false;
    }

    // Split the host into segments based on '.'
    std::vector<absl::string_view> segments;
    size_t start = 0;
    size_t end = host.find('.');
    while (end != absl::string_view::npos) {
        segments.push_back(host.substr(start, end - start));
        start = end + 1;
        end = host.find('.', start);
    }
    segments.push_back(host.substr(start, end));

    // Check if the host has exactly 3 segments, starts with "receiver", and ends with "svc"
    if (!(segments.size() == 3 && segments[0] == expected_service_name && segments[2] == "svc")) {
        return false;
    }
    // Check if the path starts with "/poll"
    if (!starts_with(path, "/poll")) {
        return false;
    }

    domain_id = std::string(segments[1]);

    return true;
}

PollerFilter::PollerFilter(const PollerConfigPbConfig &config, Upstream::ClusterManager & cluster_manager, TimeSource& time_source)
    : receiver_service_name_(config.receiver_service_name()), req_timeout_(config.request_timeout()), rsp_timeout_(config.response_timeout()),
    heartbeat_interval_(config.heartbeat_interval()), cluster_manager_(cluster_manager), time_source_(time_source)
{
    if (receiver_service_name_.size() == 0) {
        receiver_service_name_ = "receiver";
    }
    if (req_timeout_ <= 0) {
        req_timeout_ = 30;
    }
    if (rsp_timeout_ <= 0) {
        rsp_timeout_ = 30;
    }
    if (heartbeat_interval_ <= 0) {
        heartbeat_interval_ = 25;
    }

    for (const auto& source_headers : config.append_headers()) {
        std::vector<std::pair<std::string, std::string>> headers;
        headers.reserve(source_headers.headers_size());
        for (const auto& entry : source_headers.headers()) {
            headers.emplace_back(entry.key(), entry.value());
        }
        append_headers_.emplace(source_headers.source(), headers);
    }
}

PollerFilter::~PollerFilter()
{
    if (response_timer_) {
        response_timer_->disableTimer();
    }
}

Http::FilterHeadersStatus PollerFilter::decodeHeaders(Http::RequestHeaderMap &headers, bool)
{
    auto host = headers.getHostValue();
    auto path = headers.getPathValue();

    if (isPollRequest(host, path, receiver_service_name_, peer_domain_))
    {
        auto query_params = Http::Utility::parseQueryString(path);
        auto svc = query_params.find(KusciaCommon::ServiceParamKey);

        Envoy::SystemTime system_time = time_source_.systemTime();
        std::chrono::seconds seconds_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(
            system_time.time_since_epoch());
        std::string current_timestamp = std::to_string(seconds_since_epoch.count());
        if (svc == query_params.end() && svc->second.empty()) {
            conn_id_ = "unknown:" + peer_domain_ + ":" + current_timestamp;
        } else {
            conn_id_ = std::string(svc->second) + ":" + peer_domain_ + ":" + current_timestamp;
        }
        peer_receiver_host_ = receiver_service_name_ + "." + peer_domain_ + ".svc";
        forward_response_ = true;
        ENVOY_LOG(info, "[{}] Poller begin to forward response, host: {}, path: {}, peer_domain_: {}", conn_id_, host, path, peer_domain_);
    }

    return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus PollerFilter::encodeHeaders(Http::ResponseHeaderMap &headers, bool)
{
    if (forward_response_)
    {
        const auto status = headers.getStatusValue();
        ENVOY_LOG(info, "[{}] Poller status: {}", conn_id_, status);

        if (status == "200")
        {
            encoder_callbacks_->setEncoderBufferLimit(1024 * 1024 * 100);
            response_timer_ = encoder_callbacks_->dispatcher().createTimer([this]() -> void {
                sendHeartbeat();
            });
            response_timer_->enableTimer(std::chrono::seconds(heartbeat_interval_));
        } else {
            forward_response_ = false;
        }
    }

    return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus PollerFilter::encodeData(Buffer::Instance &data, bool)
{
    if (!forward_response_)
    {
        return Http::FilterDataStatus::Continue;
    }

    while (attemptToDecodeMessage(data)){}

    return Http::FilterDataStatus::StopIterationNoBuffer;
}

bool PollerFilter::attemptToDecodeMessage(Buffer::Instance &data)
{
    if (data.length() == 0) {
        return false;
    }

    RequestMessagePb message;
    KusciaCommon::DecodeStatus status = decoder_.decode(data, message);
    if (status == KusciaCommon::DecodeStatus::Ok) {
        std::string errmsg;
        int32_t status_code = forwardMessage(message, errmsg);
        if (status_code != 200) {
            ENVOY_LOG(error, "[{}] Forward message {} to {}{} error: {}", conn_id_, message.id(), message.host(), message.path(), errmsg);

            ResponseMessagePb resp_msg_pb;
            resp_msg_pb.set_status_code(status_code);
            resp_msg_pb.set_end_stream(true);
            std::string errmsg;
            if (!replyToReceiver(conn_id_, cluster_manager_, message.id(), peer_receiver_host_, resp_msg_pb, rsp_timeout_, errmsg)) {
                ENVOY_LOG(error, "[{}] Reply to receiver error: {},  message id: {}", conn_id_, message.id(), errmsg);
            }
        }
        return true;
    } else if (status == KusciaCommon::DecodeStatus::NeedMoreData) {
        ENVOY_LOG(info, "[{}] Decode message need more data", conn_id_);
        return false;
    } else {
        ENVOY_LOG(error, "[{}] Decode message error code: {}", conn_id_, KusciaCommon::decodeStatusString(status));
        encoder_callbacks_->resetStream();
        return false;
    }

    return false;
}

bool parseKusciaHost(const std::string& host, std::string &service) {
    // Split the host into segments based on '.'
    std::vector<std::string> segments;
    size_t start = 0;
    size_t end = host.find('.');
    while (end != std::string::npos) {
        segments.push_back(host.substr(start, end - start));
        start = end + 1;
        end = host.find('.', start);
    }
    segments.push_back(host.substr(start, end));

    if (segments.size() < 1) {
        return false;
    }

    service = segments[0];

    return true;
}

int32_t PollerFilter::forwardMessage(const RequestMessagePb &message, std::string& errmsg)
{
    std::string host = message.host();

    ENVOY_LOG(info, "[{}] Forward message {} to {}{}, method: {}", conn_id_, message.id(), message.host(), message.path(), message.method());

    std::string service_name;
    if (!parseKusciaHost(host, service_name)) {
        errmsg = "parse kuscia host " + host + " error";
        return 500;
    }
    std::string cluster_name = "service-" + service_name;
    // TODO check service_name
    if (message.path() == "/handshake") {
        cluster_name = "handshake-cluster";
    }

    // Ensure the existence of the target cluster
    // TODO Not considering domain transit
    Upstream::ThreadLocalCluster* cluster = cluster_manager_.getThreadLocalCluster(cluster_name);
    if (cluster == nullptr) {
        errmsg = "cluster " + cluster_name + " not found";
        return 404;
    }

    // Get asynchronous HTTP client
    Http::AsyncClient& client = cluster->httpAsyncClient();

    // Construct request message
    Http::RequestMessagePtr req_msg(new Http::RequestMessageImpl());

    for (const auto& header : message.headers()) {
        // Add each header to the request's headers
        req_msg->headers().addCopy(
            Envoy::Http::LowerCaseString(header.first),
            header.second
        );
    }

    req_msg->headers().setPath(message.path());
    req_msg->headers().setHost(message.host());
    req_msg->headers().setMethod(message.method());
    req_msg->body().add(message.body());

    if (service_name == "apiserver") {
        return forwardToApiserver(client, req_msg, message.id(), errmsg);
    } else {
        return forwardToApplication(client, req_msg, message.id(), errmsg);
    }
}

int32_t PollerFilter::forwardToApplication(Http::AsyncClient& client, Http::RequestMessagePtr& req_msg, const std::string& msg_id, std::string& errmsg)
{
    // Send asynchronous requests
    ApplicationCallbacks* callbacks = new ApplicationCallbacks(conn_id_, cluster_manager_, msg_id, peer_receiver_host_, rsp_timeout_);
    Envoy::Http::AsyncClient::RequestOptions options;
    options.setTimeout(std::chrono::milliseconds(req_timeout_ * 1000));
    Http::AsyncClient::Request* request = client.send(std::move(req_msg), *callbacks, options);
    if (request == nullptr) {
        delete callbacks;
        callbacks = nullptr;
        errmsg = "can't create request";
        return 500;
    }

    return 200;
}

int32_t PollerFilter::forwardToApiserver(Http::AsyncClient& client, Http::RequestMessagePtr& req_msg, const std::string& msg_id, std::string& errmsg)
{
    // TODO Not considering domain transit
    appendHeaders(req_msg->headers());

    // Send asynchronous requests
    ApiserverCallbacks* callbacks = new ApiserverCallbacks(conn_id_, cluster_manager_, msg_id, peer_receiver_host_, rsp_timeout_);
    Envoy::Http::AsyncClient::StreamOptions options;
    options.setTimeout(std::chrono::milliseconds(6 * 60 * 1000));

    // TODO How to know if the client connection is released
    Envoy::Http::AsyncClient::Stream* stream = client.start(*callbacks, options);
    if (!stream) {
        delete callbacks;
        callbacks = nullptr;
        errmsg = "can't create stream request";
        return 500;
    }

    stream->sendHeaders(req_msg->headers(), false);

    stream->sendData(req_msg->body(), true);

    callbacks->saveRequestMessage(std::move(req_msg));

    return 200;
}

void PollerFilter::appendHeaders(Http::RequestHeaderMap& headers) {
    auto iter = append_headers_.find(peer_domain_);
    if (iter != append_headers_.end()) {
        for (const auto& entry : iter->second) {
            headers.addCopy(Http::LowerCaseString(entry.first), entry.second);
        }
    }
}

void PollerFilter::sendHeartbeat() {
    Buffer::OwnedImpl hello_data("hello");
    encoder_callbacks_->injectEncodedDataToFilterChain(hello_data, false);

    response_timer_->enableTimer(std::chrono::seconds(heartbeat_interval_));
}


} // namespace KusciaPoller
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
