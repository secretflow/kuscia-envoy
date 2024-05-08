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

#include "kuscia/source/filters/http/kuscia_gress/gress_filter.h"
#include "fmt/format.h"
#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/types.h>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaGress {

static std::string replaceNamespaceInHost(absl::string_view host,
                                          absl::string_view new_namespace) {
  std::vector<absl::string_view> fields = absl::StrSplit(host, ".");
  for (std::size_t i = 2; i < fields.size(); i++) {
    if (fields[i] == "svc") {
      fields[i - 1] = new_namespace;
      return absl::StrJoin(fields, ".");
    }
  }
  return "";
}

static std::string getGatewayDesc(const std::string& domain, const std::string& instance,
                                  const std::string& listener) {
  return fmt::format("{}/{}/{}", domain, instance, listener);
}

static std::string getListener(const StreamInfo::StreamInfo& stream_info) {
  std::string address;
  auto& provider = stream_info.downstreamAddressProvider();
  if (provider.localAddress() != nullptr) {
    address = provider.localAddress()->asString();
  }
  if (address.empty()) {
    return "-";
  }
  return absl::EndsWith(address, ":80") ? "internal" : "external";
}

static std::string getCause(const StreamInfo::StreamInfo& stream_info) {
  std::string cause;
  if (stream_info.responseCodeDetails().has_value()) {
    cause = stream_info.responseCodeDetails().value();
  }
  return cause;
}

std::string strip(absl::string_view sv) { return std::string(sv.data(), sv.size()); }

std::string getHeaderValue(const Http::ResponseHeaderMap& headers,
                           const Http::LowerCaseString& key) {
  auto value_header = headers.get(key);
  if (!value_header.empty() && value_header[0] != nullptr && !value_header[0]->value().empty()) {
    return strip(value_header[0]->value().getStringView());
  }
  return "";
}

RewriteHostConfig::RewriteHostConfig(const RewriteHost& config)
    : rewrite_policy_(config.rewrite_policy()), header_(config.header()),
      specified_host_(config.specified_host()) {
  path_matchers_.reserve(config.path_matchers_size());
  for (const auto& pm : config.path_matchers()) {
    PathMatcherConstSharedPtr matcher(new Envoy::Matchers::PathMatcher(pm));
    path_matchers_.emplace_back(matcher);
  }
}

GressFilterConfig::GressFilterConfig(const GressPbConfig& config)
    : instance_(config.instance()), self_namespace_(config.self_namespace()),
      add_origin_source_(config.add_origin_source()),
      max_logging_body_size_per_reqeuest_(config.max_logging_body_size_per_reqeuest()) {
  rewrite_host_config_.reserve(config.rewrite_host_config_size());
  for (const auto& rh : config.rewrite_host_config()) {
    rewrite_host_config_.emplace_back(RewriteHostConfig(rh));
  }
}

Http::FilterHeadersStatus GressFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  // store some useful headers
  request_id_ = std::string(headers.getRequestIdValue());
  host_ = std::string(headers.getHostValue());
  auto record = headers.getByKey(KusciaCommon::HeaderKeyRecordBody);
  if (record.has_value() && record.value() == "true") {
    record_request_body_ = true;
    record_response_body_ = true;
  }

  // rewrite host to choose a new route
  if (rewriteHost(headers)) {
    decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
  } else {
    // replace ".svc:" with ".svc" for internal request
    size_t n = host_.rfind(".svc:");
    if (n != std::string::npos) {
      std::string substr = host_.substr(0, n + 4);
      headers.setHost(substr);
      decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
    }
  }

  // add origin-source if not exist
  if (config_->addOriginSource()) {
    auto origin_source =
        headers.getByKey(KusciaCommon::HeaderKeyOriginSource).value_or(std::string());
    if (origin_source.empty()) {
      headers.addCopy(KusciaCommon::HeaderKeyOriginSource, config_->selfNamespace());
    }
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus GressFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (record_request_body_) {
    record_request_body_ = recordBody(req_body_, data, end_stream, true);
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterHeadersStatus GressFilter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  uint64_t status_code = 0;
  if (absl::SimpleAtoi(headers.getStatusValue(), &status_code)) {
    if (!(status_code >= 400 && status_code < 600)) {
      return Http::FilterHeadersStatus::Continue;
    }
  }
  // 1. if error message key is set in response, then use it as error message
  // 2. if internal error message key is set in response, then use it as error message
  // 3. if neither of above, then use default error message
  std::string error_message = getHeaderValue(headers, KusciaCommon::HeaderKeyErrorMessage);
  bool formatted = false;
  if (!error_message.empty()) {
    formatted = true;
  } else {
    error_message = getHeaderValue(headers, KusciaCommon::HeaderKeyErrorMessageInternal);
    if (error_message.empty()) {
      error_message = Http::CodeUtility::toString(static_cast<Http::Code>(status_code));
    } else {
      headers.remove(KusciaCommon::HeaderKeyErrorMessageInternal);
    }
  }
  auto& stream_info = encoder_callbacks_->streamInfo();
  std::string rich_message = getRichMessage(stream_info, error_message, formatted);
  headers.setCopy(KusciaCommon::HeaderKeyErrorMessage, rich_message);
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus GressFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (record_response_body_) {
    record_response_body_ = recordBody(resp_body_, data, end_stream, false);
  }
  return Http::FilterDataStatus::Continue;
}

// The presence of trailers means the stream is ended, but encodeData()
// is never called with end_stream=true.
Http::FilterTrailersStatus GressFilter::encodeTrailers(Http::ResponseTrailerMap&) {
  if (record_response_body_) {
    Buffer::OwnedImpl data;
    record_response_body_ = recordBody(resp_body_, data, true, false);
  }
  return Http::FilterTrailersStatus::Continue;
}

std::string GressFilter::getRichMessage(const StreamInfo::StreamInfo& stream_info,
                                        const std::string& error_message, bool formatted) {
  std::string listener = getListener(stream_info);
  std::string gateway_desc =
      getGatewayDesc(config_->selfNamespace(), config_->instance(), listener);
  std::string cause = getCause(stream_info);
  std::string rich_message;
  if (formatted) {
    rich_message = fmt::format("<{}> => {}", gateway_desc, error_message);
  } else if (cause == "via_upstream") {
    rich_message = fmt::format("<{}> => <upstream {}>", gateway_desc, error_message);
  } else {
    rich_message = fmt::format("<{} ${}$ {}>", gateway_desc, cause, error_message);
  }
  return rich_message;
}

bool GressFilter::rewriteHost(Http::RequestHeaderMap& headers) {
  for (const auto& rh : config_->rewriteHostConfig()) {
    if (rewriteHost(headers, rh)) {
      return true;
    }
  }
  return false;
}

bool GressFilter::rewriteHost(Http::RequestHeaderMap& headers, const RewriteHostConfig& rh) {
  auto header_value = headers.getByKey(Http::LowerCaseString(rh.header())).value_or("");
  if (header_value.empty()) {
    return false;
  }

  if (rh.pathMatchers().size() > 0) {
    const absl::string_view path = headers.getPathValue();
    bool path_match = false;
    for (const auto& pm : rh.pathMatchers()) {
      if (pm->match(path)) {
        path_match = true;
        break;
      }
    }
    if (!path_match) {
      return false;
    }
  }

  switch (rh.rewritePolicy()) {
  case RewriteHost::RewriteHostWithHeader: {
    headers.setHost(header_value);
    return true;
  }
  case RewriteHost::RewriteNamespaceWithHeader: {
    auto host_value = replaceNamespaceInHost(headers.getHostValue(), header_value);
    if (!host_value.empty()) {
      headers.setHost(host_value);
      return true;
    }
    break;
  }
  case RewriteHost::RewriteHostWithSpecifiedHost: {
    if (!rh.specifiedHost().empty()) {
      headers.setHost(rh.specifiedHost());
      return true;
    }
    break;
  }
  default:
    break;
  }

  return false;
}

bool GressFilter::recordBody(Buffer::OwnedImpl& body, Buffer::Instance& data, bool end_stream,
                             bool is_req) {
  auto& stream_info = is_req ? decoder_callbacks_->streamInfo() : encoder_callbacks_->streamInfo();
  std::string body_key = is_req ? "request_body" : "response_body";

  uint64_t logging_size = static_cast<uint64_t>(config_->maxLoggingBodySizePerReqeuest());
  bool record_body = true;
  if (data.length() > 0) {
    if (logging_size > 0 && body.length() + data.length() > logging_size) {
      ENVOY_LOG(info, "{} of {} already larger than {}, stop logging", body_key, request_id_,
                logging_size);
      record_body = false;
      Buffer::OwnedImpl empty_buffer{};
      empty_buffer.move(body);
    } else {
      body.add(data);
    }
  }

  if (end_stream && body.length() > 0) {
    ProtobufWkt::Value value;
    value.set_string_value(body.toString());
    ProtobufWkt::Struct metadata;
    (*metadata.mutable_fields())[body_key] = value;
    stream_info.setDynamicMetadata("envoy.kuscia", metadata);
  }
  return record_body;
}

} // namespace KusciaGress
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
