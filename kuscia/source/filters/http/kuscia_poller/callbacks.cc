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

#include "kuscia/source/filters/http/kuscia_poller/callbacks.h"
#include "callbacks.h"
#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaPoller {

bool replyToReceiver(const std::string& conn_id, const std::string& req_host,
                     Upstream::ClusterManager& cluster_manager, const std::string& msg_id,
                     const std::string& host, const ResponseMessagePb& resp_msg_pb, int timeout,
                     std::string& errmsg) {
  // Ensure the existence of the target cluster
  std::string cluster_name = "internal-cluster";
  Upstream::ThreadLocalCluster* cluster = cluster_manager.getThreadLocalCluster(cluster_name);
  if (cluster == nullptr) {
    errmsg = "cluster " + cluster_name + " not found";
    return false;
  }

  // Get asynchronous HTTP client
  Http::AsyncClient& client = cluster->httpAsyncClient();

  // Construct request message
  Http::RequestMessagePtr req_msg(new Http::RequestMessageImpl());
  req_msg->headers().setPath("/reply?msgid=" + msg_id);
  req_msg->headers().setHost(host);
  req_msg->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
  req_msg->headers().setReferenceContentType(
      Envoy::Http::Headers::get().ContentTypeValues.Protobuf);
  req_msg->headers().setReferenceKey(KusciaCommon::HeaderTransitHash, req_host);

  std::string serialized_data = resp_msg_pb.SerializeAsString();

  req_msg->body().add(serialized_data.data(), serialized_data.size());

  // Send asynchronous requests
  ReceiverCallbacks* callbacks = new ReceiverCallbacks(conn_id, msg_id);
  Envoy::Http::AsyncClient::RequestOptions options;
  options.setTimeout(std::chrono::milliseconds(timeout * 1000));
  Http::AsyncClient::Request* request = client.send(std::move(req_msg), *callbacks, options);
  if (request == nullptr) {
    delete callbacks;
    callbacks = nullptr;
    errmsg = "can't create request";
    return false;
  }

  return true;
}

ApplicationCallbacks::~ApplicationCallbacks() {
  ENVOY_LOG(debug, "[{}] ApplicationCallbacks destroyed, message id: {}", conn_id_, message_id_);
}

void KusciaPoller::ApplicationCallbacks::onSuccess(const Http::AsyncClient::Request&,
                                                   Http::ResponseMessagePtr&& response) {
  replyToReceiverOnSuccess(std::move(response));
  delete this;
}

void ApplicationCallbacks::replyToReceiverOnSuccess(Http::ResponseMessagePtr&& response) {
  Http::ResponseHeaderMap& headers = response->headers();
  const uint64_t status_code = Http::Utility::getResponseStatus(headers);
  if (status_code == 200) {
    ENVOY_LOG(info, "[{}] Forward request message {} successully, status code: {}", conn_id_,
              message_id_, status_code);
  } else {
    ENVOY_LOG(warn, "[{}] Forward request message {} , status code: {}", conn_id_, message_id_,
              status_code);
  }

  ResponseMessagePb resp_msg_pb;
  headers.iterate([&resp_msg_pb](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    (*resp_msg_pb.mutable_headers())[std::string(header.key().getStringView())] =
        std::string(header.value().getStringView());
    return Envoy::Http::HeaderMap::Iterate::Continue;
  });
  resp_msg_pb.set_status_code(status_code);
  resp_msg_pb.set_body(response->body().toString());
  resp_msg_pb.set_end_stream(true);

  std::string errmsg;
  if (!replyToReceiver(conn_id_, req_host_, cluster_manager_, message_id_, receiver_host_,
                       resp_msg_pb, rsp_timeout_, errmsg)) {
    ENVOY_LOG(error, "[{}] Reply to receiver error: {},  message id: {}", conn_id_, message_id_,
              errmsg);
  }
}

void ApplicationCallbacks::onFailure(const Http::AsyncClient::Request&,
                                     Http::AsyncClient::FailureReason) {
  ENVOY_LOG(error, "[{}] Forward request message {} error: network error", conn_id_, message_id_);
  replyToReceiverOnFailure();
  delete this;
}

void ApplicationCallbacks::replyToReceiverOnFailure() {
  ResponseMessagePb resp_msg_pb;
  resp_msg_pb.set_status_code(502);
  resp_msg_pb.set_end_stream(true);
  std::string errmsg;
  if (!replyToReceiver(conn_id_, req_host_, cluster_manager_, message_id_, receiver_host_,
                       resp_msg_pb, rsp_timeout_, errmsg)) {
    ENVOY_LOG(error, "[{}] Reply to receiver error: {},  message id: {}", conn_id_, message_id_,
              errmsg);
  }
}

KusciaPoller::ApiserverCallbacks::~ApiserverCallbacks() {
  ENVOY_LOG(info, "[{}] ApiserverCallbacks destroyed, message id: {}", conn_id_, message_id_);
}

void ApiserverCallbacks::onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
  ENVOY_LOG(info, "[{}] ApiserverCallbacks onHeaders, message id: {}", conn_id_, message_id_);
  if (headers == nullptr) {
    ENVOY_LOG(error, "[{}] Headers is null, message id: {}", conn_id_, message_id_);
    return;
  }

  const uint64_t status_code = Http::Utility::getResponseStatus(*headers);
  if (status_code == 200) {
    ENVOY_LOG(info, "[{}] Forward request message {} successully, status code: {}", conn_id_,
              message_id_, status_code);
  } else {
    ENVOY_LOG(warn, "[{}] Forward request message {} , status code: {}", conn_id_, message_id_,
              status_code);
  }

  ResponseMessagePb resp_msg_pb;
  headers->iterate([&resp_msg_pb](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    (*resp_msg_pb.mutable_headers())[std::string(header.key().getStringView())] =
        std::string(header.value().getStringView());
    return Envoy::Http::HeaderMap::Iterate::Continue;
  });
  resp_msg_pb.set_status_code(status_code);
  resp_msg_pb.set_chunk_data(true);
  resp_msg_pb.set_index(index_++);
  if (end_stream) {
    resp_msg_pb.set_end_stream(true);
  }

  std::string errmsg;
  if (!replyToReceiver(conn_id_, req_host_, cluster_manager_, message_id_, receiver_host_,
                       resp_msg_pb, rsp_timeout_, errmsg)) {
    ENVOY_LOG(error, "[{}] Reply to receiver error: {},  message id: {}", conn_id_, message_id_,
              errmsg);
  }
}

void ApiserverCallbacks::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(info, "[{}] ApiserverCallbacks onData, message id: {}, data len: {}", conn_id_,
            message_id_, data.length());
  ResponseMessagePb resp_msg_pb;

  resp_msg_pb.set_body(data.toString());
  resp_msg_pb.set_chunk_data(true);
  resp_msg_pb.set_index(index_++);
  if (end_stream) {
    resp_msg_pb.set_end_stream(true);
  }

  std::string errmsg;
  if (!replyToReceiver(conn_id_, req_host_, cluster_manager_, message_id_, receiver_host_,
                       resp_msg_pb, rsp_timeout_, errmsg)) {
    ENVOY_LOG(error, "[{}] Reply to receiver error: {},  message id: {}", conn_id_, message_id_,
              errmsg);
  }
}

void ApiserverCallbacks::onTrailers(Http::ResponseTrailerMapPtr&&) {
  ENVOY_LOG(info, "[{}] ApiserverCallbacks onTrailers, message id: {}", conn_id_, message_id_);
  ResponseMessagePb resp_msg_pb;
  resp_msg_pb.set_end_stream(true);
  resp_msg_pb.set_chunk_data(true);
  resp_msg_pb.set_index(index_++);

  std::string errmsg;
  if (!replyToReceiver(conn_id_, req_host_, cluster_manager_, message_id_, receiver_host_,
                       resp_msg_pb, rsp_timeout_, errmsg)) {
    ENVOY_LOG(error, "[{}] Reply to receiver error: {},  message id: {}", conn_id_, message_id_,
              errmsg);
  }
}

void ApiserverCallbacks::onReset() {
  ENVOY_LOG(info, "[{}] ApiserverCallbacks onReset, message id: {}", conn_id_, message_id_);
  ResponseMessagePb resp_msg_pb;
  resp_msg_pb.set_status_code(503);
  resp_msg_pb.set_end_stream(true);
  resp_msg_pb.set_index(index_++);

  std::string errmsg;
  if (!replyToReceiver(conn_id_, req_host_, cluster_manager_, message_id_, receiver_host_,
                       resp_msg_pb, rsp_timeout_, errmsg)) {
    ENVOY_LOG(error, "[{}] Reply to receiver error: {},  message id: {}", conn_id_, message_id_,
              errmsg);
  }

  delete this;
}

void ApiserverCallbacks::onComplete() {
  ENVOY_LOG(info, "[{}] ApiserverCallbacks onComplete, message id: {}", conn_id_, message_id_);

  delete this;
}

void ApiserverCallbacks::saveRequestMessage(Http::RequestMessagePtr&& req_message) {
  req_message_ = std::move(req_message);
}

ReceiverCallbacks::~ReceiverCallbacks() {
  ENVOY_LOG(debug, "[{}] ReceiverCallbacks destroyed, message id: {}", conn_id_, msg_id_);
}

void ReceiverCallbacks::onSuccess(const Http::AsyncClient::Request&,
                                  Http::ResponseMessagePtr&& response) {
  const uint64_t status_code = Http::Utility::getResponseStatus(response->headers());
  if (status_code == 200) {
    ENVOY_LOG(info, "[{}] Forward response message {} successully, status code: {}", conn_id_,
              msg_id_, status_code);
  } else {
    ENVOY_LOG(warn, "[{}] Forward response message {} , status code: {}", conn_id_, msg_id_,
              status_code);
  }

  delete this;
}

void ReceiverCallbacks::onFailure(const Http::AsyncClient::Request&,
                                  Http::AsyncClient::FailureReason) {
  ENVOY_LOG(error, "[{}] Forward response message {} error: {}", conn_id_, msg_id_,
            "network error");
  delete this;
}

} // namespace KusciaPoller
// namespace KusciaPoller
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy