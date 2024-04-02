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

#include "envoy/http/header_map.h"
#include "include/nlohmann/json.hpp"
#include "kuscia/api/filters/http/kuscia_receiver/v3/message.pb.h"
#include "kuscia/api/filters/http/kuscia_receiver/v3/receiver.pb.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaReceiver {

using RequestMessagePb = envoy::extensions::filters::http::kuscia_receiver::v3::RequestMessage;
using ResponseMessagePb = envoy::extensions::filters::http::kuscia_receiver::v3::ResponseMessage;
using ReceiverPbConfig = envoy::extensions::filters::http::kuscia_receiver::v3::Receiver;
using ReceiverRule = envoy::extensions::filters::http::kuscia_receiver::v3::ReceiverRule;
using ReceiverRulePtr = std::shared_ptr<ReceiverRule>;

static constexpr absl::string_view REPLY_DETAIL = "poller_receiver";

enum ReceiverEventType {
  RECEIVER_EVENT_TYPE_UNKNOWN = 0,
  RECEIVER_EVENT_TYPE_CONNECT = 1,
  RECEIVER_EVENT_TYPE_DATA_SEND = 2,
  RECEIVER_EVENT_TYPE_DISCONNECT = 3,
  RECEIVER_EVENT_TYPE_BUFFER_HIGH = 4,
  RECEIVER_EVENT_TYPE_BUFFER_LOW = 5,
  RECEIVER_EVENT_TYPE_DATA_RECV = 6,
  RECEIVER_EVENT_TYPE_TIMEOUT = 7,
  RECEIVER_EVENT_TYPE_CLOSE = 8
};

class ReceiverEvent {
public:
  ReceiverEvent() : event_type_(RECEIVER_EVENT_TYPE_UNKNOWN) {}

  ReceiverEventType getEventType() const { return event_type_; }

protected:
  ReceiverEventType event_type_;
};

using ReceiverEventPtr = std::shared_ptr<ReceiverEvent>;
using StreamDestroyPtr = std::shared_ptr<std::atomic<bool>>;

class ConnectEvent : public ReceiverEvent {
public:
  explicit ConnectEvent(Http::StreamDecoderFilterCallbacks* cbs, ReceiverRulePtr rule,
                        StreamDestroyPtr sd, const std::string& uuid)
      : cbs_(cbs), sd_(sd), rule_(rule), uuid_(uuid) {
    event_type_ = ReceiverEventType::RECEIVER_EVENT_TYPE_CONNECT;
  }

  Http::StreamDecoderFilterCallbacks* getCbs() const { return cbs_; }

  ReceiverRulePtr getRule() const { return rule_; }

  StreamDestroyPtr getStreamDestroyed() const { return sd_; }

  std::string& getUuid() { return uuid_; }

private:
  Http::StreamDecoderFilterCallbacks* cbs_;
  StreamDestroyPtr sd_;
  ReceiverRulePtr rule_;
  std::string uuid_;
};

using ConnectEventPtr = std::shared_ptr<ConnectEvent>;

class SendDataEvent : public ReceiverEvent {
public:
  explicit SendDataEvent(Http::StreamDecoderFilterCallbacks* cbs, ReceiverRulePtr rule,
                         RequestMessagePb&& req, StreamDestroyPtr sd)
      : cbs_(cbs), sd_(sd), req_(std::move(req)), rule_(rule) {
    event_type_ = ReceiverEventType::RECEIVER_EVENT_TYPE_DATA_SEND;
  }

  Http::StreamDecoderFilterCallbacks* getCbs() const { return cbs_; }

  ReceiverRulePtr getRule() const { return rule_; }

  RequestMessagePb& getReqData() { return req_; }

  StreamDestroyPtr getStreamDestroyed() const { return sd_; }

private:
  Http::StreamDecoderFilterCallbacks* cbs_;
  StreamDestroyPtr sd_;
  RequestMessagePb req_;
  ReceiverRulePtr rule_;
};

using SendDataEventPtr = std::shared_ptr<SendDataEvent>;

class DisconnectEvent : public ReceiverEvent {
public:
  explicit DisconnectEvent(ReceiverRulePtr rule, const std::string& uuid)
      : rule_(rule), uuid_(uuid) {
    event_type_ = ReceiverEventType::RECEIVER_EVENT_TYPE_DISCONNECT;
  }

  ReceiverRulePtr getRule() const { return rule_; }

  std::string& getUuid() { return uuid_; }

private:
  ReceiverRulePtr rule_;
  std::string uuid_;
};

using DisconnectEventPtr = std::shared_ptr<DisconnectEvent>;

class BufferHighEvent : public ReceiverEvent {
public:
  explicit BufferHighEvent(ReceiverRulePtr rule) : rule_(rule) {
    event_type_ = ReceiverEventType::RECEIVER_EVENT_TYPE_BUFFER_HIGH;
  }

  ReceiverRulePtr getRule() const { return rule_; }

private:
  ReceiverRulePtr rule_;
};

using BufferHighEventPtr = std::shared_ptr<BufferHighEvent>;

class BufferLowEvent : public ReceiverEvent {
public:
  explicit BufferLowEvent(ReceiverRulePtr rule) : rule_(rule) {
    event_type_ = ReceiverEventType::RECEIVER_EVENT_TYPE_BUFFER_LOW;
  }

  ReceiverRulePtr getRule() const { return rule_; }

private:
  ReceiverRulePtr rule_;
};

using BufferLowEventPtr = std::shared_ptr<BufferLowEvent>;

class RecvDataEvent : public ReceiverEvent {
public:
  explicit RecvDataEvent(ResponseMessagePb&& resp, std::string request_id)
      : resp_(std::move(resp)), request_id_(std::move(request_id)) {
    event_type_ = ReceiverEventType::RECEIVER_EVENT_TYPE_DATA_RECV;
  }

  ResponseMessagePb& getRespData() { return resp_; }

  std::string& getRequestId() { return request_id_; }

  bool operator>(const RecvDataEvent& other) const { return resp_.index() > other.resp_.index(); }

private:
  ResponseMessagePb resp_;
  std::string request_id_;
};

using RecvDataEventPtr = std::shared_ptr<RecvDataEvent>;

class TimeoutEvent : public ReceiverEvent {
public:
  explicit TimeoutEvent(ReceiverRulePtr rule, const std::string& uuid) : rule_(rule), uuid_(uuid) {
    event_type_ = ReceiverEventType::RECEIVER_EVENT_TYPE_TIMEOUT;
  }

  ReceiverRulePtr getRule() const { return rule_; }

  std::string& getUuid() { return uuid_; }

private:
  ReceiverRulePtr rule_;
  std::string uuid_;
};

using TimeoutEventPtr = std::shared_ptr<TimeoutEvent>;

class CloseEvent : public ReceiverEvent {
public:
  explicit CloseEvent(std::string request_id) : request_id_(std::move(request_id)) {
    event_type_ = ReceiverEventType::RECEIVER_EVENT_TYPE_CLOSE;
  }

  std::string& getRequestId() { return request_id_; }

private:
  std::string request_id_;
};

using CloseEventPtr = std::shared_ptr<CloseEvent>;

} // namespace KusciaReceiver
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy