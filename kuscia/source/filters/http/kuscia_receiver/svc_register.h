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
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"
#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"
#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include <chrono>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaReceiver {

class SVCRegister : public Http::AsyncClient::Callbacks {
public:
  explicit SVCRegister(Upstream::ClusterManager& cm) : cm_(cm) {}

  void registerSVC(Http::RequestMessagePtr message) {
    const auto cluster = cm_.getThreadLocalCluster(KusciaCommon::GatewayClusterName);
    if (cluster != nullptr) {
      cluster->httpAsyncClient().send(
          std::move(message), *this,
          Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(1000)));
    }
  }

  void unregisterSVC(Http::RequestMessagePtr message) {
    const auto cluster = cm_.getThreadLocalCluster(KusciaCommon::GatewayClusterName);
    if (cluster != nullptr) {
      cluster->httpAsyncClient().send(
          std::move(message), *this,
          Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(1000)));
    }
  }

private:
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override {}
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override {}
  void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                    const Http::ResponseHeaderMap*) override {}

private:
  Upstream::ClusterManager& cm_;
};

static std::string getLocalhost() {
  std::string local_ip;
  if (!Api::OsSysCallsSingleton::get().supportsGetifaddrs()) {
    return local_ip;
  }
  // iteration over ifaddrs
  Api::InterfaceAddressVector interface_addresses{};
  const Api::SysCallIntResult rc = Api::OsSysCallsSingleton::get().getifaddrs(interface_addresses);
  if (rc.return_value_ == -1) {
    return local_ip;
  }
  for (const auto& addr : interface_addresses) {
    if (addr.interface_name_ == "eth0") {
      return addr.interface_addr_->ip()->addressAsString();
    }
  }
  return local_ip;
}

static size_t curlCallback(char* ptr, size_t, size_t nmemb, void* data) {
  auto buf = static_cast<std::string*>(data);
  buf->append(ptr, nmemb);
  return nmemb;
}

static absl::optional<std::string> doCurl(Http::RequestMessagePtr message) {
  static const size_t MAX_RETRIES = 2;
  static const std::chrono::milliseconds RETRY_DELAY{1000};
  static const std::chrono::seconds TIMEOUT{5};

  CURL* const curl = curl_easy_init();
  if (!curl) {
    return absl::nullopt;
  };

  const auto host = message->headers().getHostValue();
  const auto path = message->headers().getPathValue();
  const auto method = message->headers().getMethodValue();

  const std::string url = fmt::format("http://{}{}", host, path);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, TIMEOUT.count());
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  std::string buffer(message->body().toString());
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlCallback);

  struct curl_slist* headers = nullptr;
  message->headers().iterate(
      [&headers](const Http::HeaderEntry& entry) -> Http::HeaderMap::Iterate {
        // Skip pseudo-headers
        if (!entry.key().getStringView().empty() && entry.key().getStringView()[0] == ':') {
          return Http::HeaderMap::Iterate::Continue;
        }
        const std::string header =
            fmt::format("{}: {}", entry.key().getStringView(), entry.value().getStringView());
        headers = curl_slist_append(headers, header.c_str());
        return Http::HeaderMap::Iterate::Continue;
      });

  if (Http::Headers::get().MethodValues.Put == method) {
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE, 0);
    headers = curl_slist_append(headers, "Expect:");
  }

  if (headers != nullptr) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }

  for (size_t retry = 0; retry < MAX_RETRIES; retry++) {
    const CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
      break;
    }
    buffer.clear();
    std::this_thread::sleep_for(RETRY_DELAY);
  }

  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);

  return buffer.empty() ? absl::nullopt : absl::optional<std::string>(buffer);
}

Http::RequestMessagePtr createRegisterRequest(ReceiverRulePtr rule) {
  Http::RequestMessagePtr message(new Http::RequestMessageImpl());
  message->headers().setMethod(Http::Headers::get().MethodValues.Post);
  message->headers().setPath(KusciaCommon::GatewayRegisterPath);
  message->headers().setHost(KusciaCommon::GatewayHostName);
  message->headers().setContentType(Http::Headers::get().ContentTypeValues.Json);
  nlohmann::json json = {"src", rule->source(),  "dst",  rule->destination(),
                         "svc", rule->service(), "host", getLocalhost()};
  message->body().add(json.dump());
  return message;
}

Http::RequestMessagePtr createUnregisterRequest(ReceiverRulePtr rule) {
  Http::RequestMessagePtr message(new Http::RequestMessageImpl());
  message->headers().setMethod(Http::Headers::get().MethodValues.Post);
  message->headers().setPath(KusciaCommon::GatewayUnregisterPath);
  message->headers().setHost(KusciaCommon::GatewayHostName);
  message->headers().setContentType(Http::Headers::get().ContentTypeValues.Json);
  nlohmann::json json = {"src", rule->source(),  "dst",  rule->destination(),
                         "svc", rule->service(), "host", getLocalhost()};
  message->body().add(json.dump());
  return message;
}

void curlRegister(ReceiverRulePtr rule, bool block) {
  Http::RequestMessagePtr message = createRegisterRequest(rule);
  if (!block) {
    std::thread([message{std::move(message)}]() mutable { doCurl(std::move(message)); }).detach();
  } else {
    doCurl(std::move(message));
  }
}

void curlUnregister(ReceiverRulePtr rule, bool block) {
  Http::RequestMessagePtr message = createUnregisterRequest(rule);
  if (!block) {
    std::thread([message{std::move(message)}]() mutable { doCurl(std::move(message)); }).detach();
  } else {
    doCurl(std::move(message));
  }
}

void nativeRegister(ReceiverRulePtr rule, Upstream::ClusterManager& cm) {
  SVCRegister svc_register(cm);
  svc_register.registerSVC(createRegisterRequest(rule));
}

void nativeUnregister(ReceiverRulePtr rule, Upstream::ClusterManager& cm) {
  SVCRegister svc_register(cm);
  svc_register.unregisterSVC(createUnregisterRequest(rule));
}

} // namespace KusciaReceiver
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
