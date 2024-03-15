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

#include "source/common/http/codes.h"
#include "fmt/format.h"
#include "source/common/http/header_utility.h"
#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/http/headers.h"

#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <sys/types.h>
#include <type_traits>
#include <utility>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaGress {

static std::string replaceNamespaceInHost(absl::string_view host, absl::string_view new_namespace) {
    std::vector<absl::string_view> fields = absl::StrSplit(host, ".");
    for (std::size_t i = 2; i < fields.size(); i++) {
        if (fields[i] == "svc") {
            fields[i - 1] = new_namespace;
            return absl::StrJoin(fields, ".");
        }
    }
    return "";
}

RewriteHostConfig::RewriteHostConfig(const RewriteHost& config) :
    rewrite_policy_(config.rewrite_policy()),
    header_(config.header()),
    specified_host_(config.specified_host()) {
    path_matchers_.reserve(config.path_matchers_size());
    for (const auto& pm : config.path_matchers()) {
        PathMatcherConstSharedPtr matcher(new Envoy::Matchers::PathMatcher(pm));
        path_matchers_.emplace_back(matcher);
    }
}

GressFilterConfig::GressFilterConfig(const GressPbConfig& config) :
    instance_(config.instance()),
    self_namespace_(config.self_namespace()),
    add_origin_source_(config.add_origin_source()),
    max_logging_body_size_per_reqeuest_(config.max_logging_body_size_per_reqeuest()) {
    rewrite_host_config_.reserve(config.rewrite_host_config_size());
    for (const auto& rh : config.rewrite_host_config()) {
        rewrite_host_config_.emplace_back(RewriteHostConfig(rh));
    }
}

Http::FilterHeadersStatus GressFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                     bool) {
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
        auto origin_source = headers.getByKey(KusciaCommon::HeaderKeyOriginSource)
                             .value_or(std::string());
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

Http::FilterHeadersStatus GressFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                     bool end_stream) {
    if (encoder_callbacks_->streamInfo().protocol() == Http::Protocol::Http10) {
        is_http_1_0_ = true;
        return Http::FilterHeadersStatus::Continue;
    }
    if (absl::SimpleAtoi(headers.getStatusValue(), &status_code_)) {
        if (!(status_code_ >= 400 && status_code_ < 600)) {
            return Http::FilterHeadersStatus::Continue;
        }
    }
    std::string error_message;
    auto kuscia_error = getHeaderValue(headers, KusciaCommon::HeaderKeyErrorMessage);
    if (!kuscia_error.empty()) {
        error_message = kuscia_error;
    } else {
        auto internal_error = getHeaderValue(headers, KusciaCommon::HeaderKeyErrorMessageInternal);
        if (!internal_error.empty()) {
            headers.remove(KusciaCommon::HeaderKeyErrorMessageInternal);
            error_message = internal_error;
        } else {
            error_message = Http::CodeUtility::toString(static_cast<Http::Code>(status_code_));
        }
    }
    auto format_flag = getHeaderValue(headers, KusciaCommon::HeaderKeyFmtError);
    if (format_flag == "true") {
        is_err_formatted_ = true;
    }
    if (encoder_callbacks_->streamInfo().protocol() == Http::Protocol::Http11) {
        headers.setTransferEncoding(Http::Headers::get().TransferEncodingValues.Chunked);
    }
    headers.setCopy(KusciaCommon::HeaderKeyFmtError, "true");
    headers.removeContentLength();
    headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
    if (end_stream) {
        Buffer::OwnedImpl empty_buffer;
        buildAndFlushTrace(empty_buffer, std::move(error_message), true);
        encoder_callbacks_->addEncodedData(empty_buffer, true);
    }
    return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus GressFilter::encodeData(Buffer::Instance& data, bool end_stream) {
    if (record_response_body_) {
        record_response_body_ = recordBody(resp_body_, data, end_stream, false);
    }
    if (is_http_1_0_) {
        return Http::FilterDataStatus::Continue;
    }
    if (!(status_code_ >= 400 && status_code_ < 600)) {
        return Http::FilterDataStatus::Continue;
    }
    if (data.length() > 0) {
        err_resp_body_.add(data);
        data.drain(data.length());
    }
    if (end_stream) {
        buildAndFlushTrace(data, err_resp_body_.toString(), false);
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
    if (!(status_code_ >= 400 && status_code_ < 600)) {
        return Http::FilterTrailersStatus::Continue;
    }
    Buffer::OwnedImpl empty_buffer;
    buildAndFlushTrace(empty_buffer, err_resp_body_.toString(), false);
    encoder_callbacks_->addEncodedData(empty_buffer, true);
    return Http::FilterTrailersStatus::Continue;
}

void GressFilter::buildAndFlushTrace(Buffer::Instance& data, std::string&& err_body, bool header) {
    nlohmann::json json;
    if (!header && is_err_formatted_ && err_body.size() > 0) {
        json = nlohmann::json::parse(err_body, nullptr, false, false);
    }
    if (json.is_null()) {
        json = makeTraceBody(status_code_, std::move(err_body));
    }
    auto item = fromStreamInfo(encoder_callbacks_->streamInfo(), config_->selfNamespace());
    appendToTrace(json, item);
    data.add(dump(json));
}

std::string GressFilter::dump(const nlohmann::json& json, bool pretty) {
    if (pretty) {
        return json.dump(4, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    return json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string GressFilter::strip(absl::string_view sv) {
    return std::string(sv.data(), sv.size());
}

std::string GressFilter::getHeaderValue(const Http::ResponseHeaderMap& headers, const Http::LowerCaseString& key) {
    auto value_header = headers.get(key);
    if (!value_header.empty() && value_header[0] != nullptr && !value_header[0]->value().empty()) {
        return strip(value_header[0]->value().getStringView());
    }
    return "";
}

nlohmann::json GressFilter::fromStreamInfo(StreamInfo::StreamInfo& stream_info, const std::string& domain) {
    std::string downstream = "-";
    std::string upstream = "-";
    std::string ulocal = "-";
    std::string dlocal = "-";
    std::string method = "-";
    std::string host = "-";
    std::string path = "-";
    std::string cause;

    uint64_t flag = 0;

    auto headers = stream_info.getRequestHeaders();
    if (headers != nullptr) {
        method = strip(headers->getMethodValue());
        host = strip(headers->getHostValue());
        path = strip(headers->getPathValue());
    }

    if (stream_info.hasAnyResponseFlag()) {
        flag = stream_info.responseFlags();
    }
    if (stream_info.responseCodeDetails().has_value()) {
        cause = stream_info.responseCodeDetails().value();
    }

    auto upstream_info = stream_info.upstreamInfo();
    if (upstream_info != nullptr) {
        if (upstream_info->upstreamLocalAddress() != nullptr) {
            ulocal = upstream_info->upstreamLocalAddress()->asString();
        }
        if (upstream_info->upstreamRemoteAddress() != nullptr) {
            upstream = upstream_info->upstreamRemoteAddress()->asString();
        }
    }

    auto& provider = stream_info.downstreamAddressProvider();
    if (provider.localAddress() != nullptr) {
        dlocal = provider.localAddress()->asString();
    }
    if (provider.remoteAddress() != nullptr) {
        downstream = provider.remoteAddress()->asString();
    }

    auto tpl = "%Y-%m-%d %H:%M:%E3S";
    auto tz = absl::LocalTimeZone();
    auto start = absl::FormatTime(tpl, absl::FromChrono(stream_info.startTime()), tz);
    auto now = absl::FormatTime(tpl, absl::Now(), tz);

    if (downstream != "-") {
        if (isLocalhost(downstream)) {
            downstream = "localhost";
        } else {
            downstream = "remotehost";
        }
    }

    if (isValidIpAddress(host)) {
        host = "remotehost";
    }

    if (upstream != "-") {
        if (isLocalhost(upstream) && absl::EndsWith(upstream, ":80")) {
            upstream = "internal";
        } else {
            upstream = host;
        }
    }
    if (dlocal != "-") {
        if (absl::EndsWith(dlocal, ":80")) {
            dlocal = "internal";
        } else {
            dlocal = "external";
        }
    }
    if (ulocal != "-") {
        ulocal = "envoyproxy";
    }

    auto message = fmt::format("{} {} {}", method, host, path);
    auto link = fmt::format("{} -> [{} -> {}] -> {}", downstream, dlocal, ulocal, upstream);

    return {
        {"instance", config_->instance()},
        {"time_start", start},
        {"time_finish", now},
        {"domain", domain},
        {"raw", message},
        {"link", link},
        {"cause", cause},
        {"flag", flag},
    };
}

bool GressFilter::isValidIpAddress(const std::string& address) {
    // Try to convert the string into an IPv4 address
    struct sockaddr_in sa4;
    int result4 = inet_pton(AF_INET, address.c_str(), &(sa4.sin_addr));

    // Try to convert the string into an IPv6 address
    struct sockaddr_in6 sa6;
    int result6 = inet_pton(AF_INET6, address.c_str(), &(sa6.sin6_addr));

    // If either result is 1, the IP conversion was successful
    return result4 == 1 || result6 == 1;
}

bool GressFilter::isLocalhost(const std::string& address) {
    // loopback
    if (absl::StartsWith(address, "127.") || absl::StartsWith(address, "[::1]")) {
        return true;
    }
    // unix socket
    if (absl::StartsWith(address, "/")) {
        return true;
    }
    // localhost
    if (absl::StartsWith(address, "0.0.0.0") || absl::StartsWith(address, "localhost")) {
        return true;
    }
    // iteration over ifaddrs
    if (!Api::OsSysCallsSingleton::get().supportsGetifaddrs()) {
        return false;
    }
    Api::InterfaceAddressVector interface_addresses{};
    const Api::SysCallIntResult rc = Api::OsSysCallsSingleton::get().getifaddrs(interface_addresses);
    if (rc.return_value_ == -1) {
        return false;
    }
    for (const auto& addr : interface_addresses) {
        if (addr.interface_addr_->type() != Network::Address::Type::Ip) {
            continue;
        }
        if (addr.interface_addr_->ip()->version() == Network::Address::IpVersion::v4) {
            if (absl::StartsWith(address, addr.interface_addr_->asString())) {
                return true;
            }
            continue;
        }
        if (addr.interface_addr_->ip()->version() == Network::Address::IpVersion::v6) {
            if (absl::StrContains(address, addr.interface_addr_->asString())) {
                return true;
            }
            continue;
        }
    }
    return false;
}

// Generate prompt, given url should be consistent with envoy version.
std::string GressFilter::genPrompt(uint64_t flag, std::string&& detail) {
    return fmt::format("Please refer to {} ResponseFlag@<0x{:X}> ResponseCodeDetailValues@<{}> for more details.",
        "https://github.com/envoyproxy/envoy/blob/v1.25.1/envoy/stream_info/stream_info.h",
        flag,
        std::move(detail));
}

nlohmann::json GressFilter::makeTraceBody(int code, std::string&& message) {
    return {
        {"code", code},
        {"root-cause", std::move(message)},
        {"trace", nlohmann::json::array()}
    };
}

void GressFilter::appendToTrace(nlohmann::json& json, nlohmann::json& item) {
    if (!json.contains("trace")) {
        return;
    }
    auto trace = json["trace"];
    if (!trace.is_array()) {
        return;
    }
    if (trace.empty()) {
        auto cause = item["cause"];
        auto flag = item["flag"];
        if (!cause.empty() && flag.is_number_unsigned()) {
            if (cause != "direct_response" && cause != "via_upstream") {
                json["root-cause"] = cause;
            }
            item["detail"] = genPrompt(flag, std::move(cause));
        }
    }
    item.erase("flag");
    item.erase("cause");
    trace.emplace_back(item);
    json["trace"] = trace;

    ENVOY_LOG(warn, "request_id {} trivial {}", request_id_, dump(json, true));
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

bool GressFilter::recordBody(Buffer::OwnedImpl& body, Buffer::Instance& data,
                             bool end_stream, bool is_req) {
    auto& stream_info = is_req ? decoder_callbacks_->streamInfo() : encoder_callbacks_->streamInfo();
    std::string body_key = is_req ? "request_body" : "response_body";

    uint64_t logging_size = static_cast<uint64_t>(config_->maxLoggingBodySizePerReqeuest());
    bool record_body = true;
    if (data.length() > 0) {
        if (logging_size > 0 && body.length() + data.length() > logging_size) {
            ENVOY_LOG(info, "{} of {} already larger than {}, stop logging",
                      body_key, request_id_, logging_size);
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
