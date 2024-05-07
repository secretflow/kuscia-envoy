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

#include "receiver_filter.h"
#include "event.h"
#include "event_loop.h"
#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"
#include "source/common/common/logger.h"
#include "source/common/common/random_generator.h"
#include "source/common/http/utility.h"
#include <regex>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaReceiver {

static int timeout2sec(std::string& timeout) {
  static std::regex time_pattern("(?:(\\d+)h)?(?:(\\d+)m)?(?:(\\d+)s)?");
  int h = 0, m = 0, s = 0;
  std::smatch match;
  if (std::regex_match(timeout, match, time_pattern)) {
    if (match[1].matched) {
      h = std::stoi(match[1].str());
    }
    if (match[2].matched) {
      m = std::stoi(match[2].str());
    }
    if (match[3].matched) {
      s = std::stoi(match[3].str());
    }
  }
  return h * 3600 + m * 60 + s;
}

static void setReqHeader(RequestMessagePb& pb, Http::RequestHeaderMap& headers) {
  pb.set_host(std::string(headers.getHostValue()));
  pb.set_path(std::string(headers.getPathValue()));
  pb.set_method(std::string(headers.getMethodValue()));
  auto hs = pb.mutable_headers();

  headers.iterate([&hs](const Http::HeaderEntry& e) -> Http::HeaderMap::Iterate {
    auto key = std::string(e.key().getStringView());
    auto value = std::string(e.value().getStringView());
    (*hs)[key] = value;
    return Envoy::Http::HeaderMap::Iterate::Continue;
  });
}

static void setReqBody(RequestMessagePb& pb, Buffer::OwnedImpl& body) {
  pb.set_body(body.toString());
}

ReceiverFilter::ReceiverFilter(ReceiverFilterConfigSharedPtr config)
    : config_(config), event_type_(ReceiverEventType::RECEIVER_EVENT_TYPE_UNKNOWN) {
  sd_ = std::make_shared<std::atomic<bool>>(false);
}

Http::FilterHeadersStatus ReceiverFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                        bool end_stream) {
  // chunked encoding is not supported
  if (decoder_callbacks_->streamInfo().protocol() != Http::Protocol::Http11) {
    return Http::FilterHeadersStatus::Continue;
  }
  if (isPassthroughTraffic(headers)) {
    ENVOY_LOG(info, "host {} path {} method {} is passthrough traffic", headers.getHostValue(),
              headers.getPathValue(), headers.getMethodValue());
    return Http::FilterHeadersStatus::Continue;
  } else {
    ENVOY_LOG(info, "host {} path {} method {} is kuscia traffic", headers.getHostValue(),
              headers.getPathValue(), headers.getMethodValue());
  }
  if (isPollRequest(headers) || isForwardRequest(headers) || isForwardResponse(headers)) {
    if (event_type_ == ReceiverEventType::RECEIVER_EVENT_TYPE_DATA_SEND) {
      setReqHeader(request_pb_, headers);
    }
    if (end_stream) {
      postEvent();
    }
    return Http::FilterHeadersStatus::StopIteration;
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus ReceiverFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (event_type_ != ReceiverEventType::RECEIVER_EVENT_TYPE_UNKNOWN) {
    if (data.length() > 0) {
      body_.add(data);
      data.drain(data.length());
    }
    if (end_stream) {
      postEvent();
    }
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus ReceiverFilter::decodeTrailers(Http::RequestTrailerMap&) {
  if (event_type_ != ReceiverEventType::RECEIVER_EVENT_TYPE_UNKNOWN) {
    postEvent();
    return Http::FilterTrailersStatus::StopIteration;
  }
  return Http::FilterTrailersStatus::Continue;
}

void ReceiverFilter::onDestroy() {
  ReceiverEventPtr event;
  switch (event_type_) {
  case RECEIVER_EVENT_TYPE_CONNECT:
    event = std::make_shared<DisconnectEvent>(rule_, conn_uuid_);
    break;
  case RECEIVER_EVENT_TYPE_DATA_SEND:
    event = std::make_shared<CloseEvent>(request_id_);
    break;
  default:
    return;
  }
  if (event != nullptr) {
    // must be execute immediately
    sd_->store(true, std::memory_order_relaxed);
    // can be handle in async thread
    EventLoopSingleton::GetInstance().postEvent(event);
  }
}

bool ReceiverFilter::isPassthroughTraffic(Http::RequestHeaderMap& headers) {
  return headers.get(KusciaCommon::HeaderTransitFlag).empty();
}

// poll request check
// receiver.${peer}.svc/poll?timeout=xxx&service=xxx
bool ReceiverFilter::isPollRequest(Http::RequestHeaderMap& headers) {
  auto host = headers.getHostValue();
  auto path = headers.getPathValue();
  auto sourceValue = headers.get(KusciaCommon::HeaderKeyOriginSource);
  if (host.empty() || path.empty() || sourceValue.empty() || sourceValue[0]->value().empty()) {
    return false;
  }
  absl::string_view source = sourceValue[0]->value().getStringView();

  std::string group;
  if (!re2::RE2::PartialMatch(std::string(host), KusciaCommon::PollHostPattern, &group) ||
      !absl::StartsWith(path, KusciaCommon::PollPathPrefix) || group != config_->selfNamespace()) {
    return false;
  }
  auto query_params = Http::Utility::QueryParamsMulti::parseQueryString(path);
  auto svc = query_params.getFirstValue(KusciaCommon::ServiceParamKey);
  if (!svc.has_value()) {
    return false;
  }
  event_type_ = ReceiverEventType::RECEIVER_EVENT_TYPE_CONNECT;
  rule_ = std::make_shared<ReceiverRule>();
  rule_->set_source(group);
  rule_->set_destination(std::string(source));
  rule_->set_service(svc.value());
  conn_uuid_ = random_.uuid();

  auto timeout = query_params.getFirstValue(KusciaCommon::TimeoutParamKey);
  if (timeout.has_value()) {
    timeout_sec_ = timeout2sec(timeout.value());
    ENVOY_LOG(info, "[ReceiverFilter] poll request from {} to {} service {} timeout {}", group,
              source, svc.value(), timeout.value());
  }
  return true;
}

// dst = ${svc}.dest-namespace.svc
// src = src-namespace
bool ReceiverFilter::isForwardRequest(Http::RequestHeaderMap& headers) {
  auto sourceHeader = headers.get(KusciaCommon::HeaderKeyOriginSource);
  if (sourceHeader.empty()) {
    return false;
  }
  absl::string_view source = sourceHeader[0]->value().getStringView();

  absl::string_view host;
  auto hostValue = headers.getHostValue();
  // rewrite
  bool rewrite = false;
  if (absl::StartsWith(hostValue, KusciaCommon::InternalClusterHost)) {
    auto kusciaHost = headers.get(KusciaCommon::HeaderKeyKusciaHost);
    if (!kusciaHost.empty()) {
      host = kusciaHost[0]->value().getStringView();
    }
    rewrite = true;
  }

  if (std::string(source) != config_->selfNamespace() || host.empty()) {
    return false;
  }
  std::vector<absl::string_view> fields = absl::StrSplit(host, ".");
  if (fields.size() != 3 || fields[0].empty() || fields[1].empty() || fields[2] != "svc") {
    return false;
  }
  if (!config_->hasRule(source, fields[1])) {
    return false;
  }
  event_type_ = ReceiverEventType::RECEIVER_EVENT_TYPE_DATA_SEND;
  rule_ = std::make_shared<ReceiverRule>();
  rule_->set_source(std::string(source));
  rule_->set_destination(std::string(fields[1]));
  rule_->set_service(std::string(fields[0]));
  request_id_ = random_.uuid();
  if (rewrite) {
    headers.setHost(host);
  }
  return true;
}

// receiver.xx.svc/reply?msgid=xxx
bool ReceiverFilter::isForwardResponse(Http::RequestHeaderMap& headers) {
  auto path = headers.getPathValue();
  auto host = headers.getHostValue();
  if (!absl::StartsWith(path, KusciaCommon::ReplyPathPrefix) || host.empty()) {
    return false;
  }
  std::vector<absl::string_view> fields = absl::StrSplit(host, ".");
  if (fields.size() != 3 || fields[0] != "receiver" || fields[1].empty() || fields[2] != "svc") {
    return false;
  }
  if (fields[1] != config_->selfNamespace()) {
    return false;
  }
  auto query_params = Http::Utility::QueryParamsMulti::parseQueryString(path);
  auto request_id = query_params.getFirstValue(KusciaCommon::RequestIdParamKey);
  if (!request_id.has_value()) {
    return false;
  }
  event_type_ = ReceiverEventType::RECEIVER_EVENT_TYPE_DATA_RECV;
  request_id_ = request_id.value();
  ENVOY_LOG(trace, "[ReceiverFilter] forward response request_id {}", request_id_);
  return true;
}

void ReceiverFilter::postEvent() {
  ReceiverEventPtr event;
  switch (event_type_) {
  case ReceiverEventType::RECEIVER_EVENT_TYPE_CONNECT: {
    ASSERT(decoder_callbacks_->connection().has_value());
    event = std::make_shared<ConnectEvent>(decoder_callbacks_, rule_, sd_, conn_uuid_);
    if (timeout_sec_ > 0) {
      ENVOY_LOG(trace, "[ReceiverFilter] connect timeout {}", timeout_sec_);
      registerConnTimeout();
    }
    break;
  }
  case ReceiverEventType::RECEIVER_EVENT_TYPE_DATA_RECV: {
    ResponseMessagePb pb;
    if (!pb.ParseFromString(body_.toString())) {
      ENVOY_LOG(warn, "[ReceiverFilter] parse response message failed!");
      replyDirectly(Http::Code::BadRequest);
      return;
    }
    ENVOY_LOG(trace, "[ReceiverFilter] get request_id {}", request_id_);
    event = std::make_shared<RecvDataEvent>(std::move(pb), request_id_);
    replyDirectly(Http::Code::OK);
    break;
  }
  case ReceiverEventType::RECEIVER_EVENT_TYPE_DATA_SEND: {
    setReqBody(request_pb_, body_);
    request_pb_.set_id(request_id_);
    event =
        std::make_shared<SendDataEvent>(decoder_callbacks_, rule_, std::move(request_pb_), sd_);
    break;
  }
  default:
    return;
  }
  EventLoopSingleton::GetInstance().postEvent(std::move(event));
}

void ReceiverFilter::replyDirectly(Http::Code code) {
  decoder_callbacks_->sendLocalReply(
      code, // HTTP status code
      "",   // Response body text
      [](Http::ResponseHeaderMap& response_headers) {
        // Modify response headers if needed
        response_headers.setReferenceKey(Envoy::Http::LowerCaseString("content-type"),
                                         "text/plain");
      },
      absl::nullopt, // grpc_status (not needed for non-gRPC responses)
      REPLY_DETAIL); // A flag that indicates why this local reply was sent
}

// May remove later, not necessary
void ReceiverFilter::registerConnTimeout() {
  ReceiverEventPtr event = std::make_shared<TimeoutEvent>(rule_, conn_uuid_);
  Event::TimerPtr timer =
      decoder_callbacks_->dispatcher().createTimer([event{std::move(event)}]() mutable {
        EventLoopSingleton::GetInstance().postEvent(std::move(event));
      });
  std::chrono::milliseconds timeout(timeout_sec_ * 1000);
  timer->enableTimer(timeout);
}

} // namespace KusciaReceiver
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy