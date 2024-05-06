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

#include "envoy/http/async_client.h"
#include "envoy/upstream/cluster_manager.h"
#include "kuscia/source/filters/http/kuscia_poller/common.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaPoller {

extern bool replyToReceiver(const std::string& conn_id, const std::string& req_host,
                            Upstream::ClusterManager& cluster_manager, const std::string& msg_id,
                            const std::string& host, const ResponseMessagePb& resp_msg_pb,
                            int timeout, std::string& errmsg);

class ApplicationCallbacks : public Http::AsyncClient::Callbacks,
                             public Logger::Loggable<Logger::Id::filter> {
public:
  ApplicationCallbacks(const std::string& conn_id, const std::string& req_host,
                       Upstream::ClusterManager& cluster_manager, const std::string& message_id,
                       const std::string& peer_receiver_host, int rsp_timeout)
      : conn_id_(conn_id), req_host_(req_host), cluster_manager_(cluster_manager),
        message_id_(message_id), receiver_host_(peer_receiver_host), rsp_timeout_(rsp_timeout) {}
  ~ApplicationCallbacks();

  void onSuccess(const Http::AsyncClient::Request& request,
                 Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request& request,
                 Http::AsyncClient::FailureReason reason) override;
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}
  void replyToReceiverOnSuccess(Http::ResponseMessagePtr&& response);
  void replyToReceiverOnFailure();

private:
  std::string conn_id_;
  std::string req_host_;
  Upstream::ClusterManager& cluster_manager_;
  std::string message_id_;
  std::string receiver_host_;
  int rsp_timeout_;
};

class ApiserverCallbacks : public Http::AsyncClient::StreamCallbacks,
                           public Logger::Loggable<Logger::Id::filter> {
public:
  ApiserverCallbacks(const std::string& conn_id, const std::string& req_host,
                     Upstream::ClusterManager& cluster_manager, const std::string& message_id,
                     const std::string& peer_receiver_host, int rsp_timeout)
      : conn_id_(conn_id), req_host_(req_host), cluster_manager_(cluster_manager),
        message_id_(message_id), receiver_host_(peer_receiver_host), rsp_timeout_(rsp_timeout),
        index_(0) {}
  ~ApiserverCallbacks();

  void onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void onData(Buffer::Instance& data, bool end_stream) override;
  void onTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
  void onReset() override;
  void onComplete() override;

  void replyToReceiverOnSuccess(Http::ResponseMessagePtr&& response);
  void replyToReceiverOnFailure();

  void saveRequestMessage(Http::RequestMessagePtr&& req_message);

private:
  std::string conn_id_;
  std::string req_host_;
  // Http::RequestHeaderMapPtr headers_;
  Http::RequestMessagePtr req_message_;
  Upstream::ClusterManager& cluster_manager_;
  std::string message_id_;
  std::string receiver_host_;
  int rsp_timeout_;
  int index_;
};

class ReceiverCallbacks : public Http::AsyncClient::Callbacks,
                          public Logger::Loggable<Logger::Id::filter> {
public:
  ReceiverCallbacks(const std::string& conn_id, const std::string& msg_id)
      : conn_id_(conn_id), msg_id_(msg_id) {}
  ~ReceiverCallbacks();
  void onSuccess(const Http::AsyncClient::Request& request,
                 Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request& request,
                 Http::AsyncClient::FailureReason reason) override;
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

private:
  std::string conn_id_;
  std::string msg_id_;
};

} // namespace KusciaPoller
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy