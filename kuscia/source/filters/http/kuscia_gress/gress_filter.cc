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
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"

#include "kuscia/source/filters/http/kuscia_common/kuscia_header.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaGress {

static void adjustContentLength(Http::RequestOrResponseHeaderMap& headers, uint64_t delta_length) {
    auto length_header = headers.getContentLengthValue();
    if (!length_header.empty()) {
        uint64_t old_length;
        if (absl::SimpleAtoi(length_header, &old_length)) {
            if (old_length != 0) {
                headers.setContentLength(old_length + delta_length);
            }
        }
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
    if (config_.rewrite_host()) {
        auto kuscia_host = headers.getByKey(KusciaCommon::HeaderKeyKusciaHost).value_or("");
        if (!kuscia_host.empty()) {
            headers.setHost(kuscia_host);
            decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
        }
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
    if (config_.add_origin_source()) {
        auto origin_source = headers.getByKey(KusciaCommon::HeaderKeyOriginSource)
                             .value_or(std::string());
        if (origin_source.empty()) {
            headers.addCopy(KusciaCommon::HeaderKeyOriginSource, config_.self_namespace());
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
    // generate error msg
    auto result = headers.get(KusciaCommon::HeaderKeyErrorMessage);
    if (headers.getStatusValue() != "200") {
        std::string err_msg;
        auto result = headers.get(KusciaCommon::HeaderKeyErrorMessage);
        if (result.empty()) {
            auto inner_msg = headers.get(KusciaCommon::HeaderKeyErrorMessageInternal);
            if (inner_msg.size() == 1 && inner_msg[0] != nullptr && !inner_msg[0]->value().empty()) {
                err_msg = fmt::format("Domain {}.{}: {}",
                                      config_.self_namespace(),
                                      config_.instance(),
                                      inner_msg[0]->value().getStringView());
                headers.remove(KusciaCommon::HeaderKeyErrorMessageInternal);
            } else {
                err_msg = fmt::format("Domain {}.{}<--{} return http code {}.",
                                      config_.self_namespace(),
                                      config_.instance(),
                                      host_,
                                      headers.getStatusValue());
            }
        } else if (result[0] != nullptr) {
            err_msg = fmt::format("Domain {}.{}<--{}",
                                  config_.self_namespace(),
                                  config_.instance(),
                                  result[0]->value().getStringView());

        }

        headers.setCopy(KusciaCommon::HeaderKeyErrorMessage, err_msg);
        if (end_stream) {
            Envoy::Buffer::OwnedImpl body(err_msg);
            adjustContentLength(headers, body.length());
            encoder_callbacks_->addEncodedData(body, true);
            headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Text);
        }
    }
    return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus GressFilter::encodeData(Buffer::Instance& data, bool end_stream) {
    if (record_response_body_) {
        record_response_body_ = recordBody(resp_body_, data, end_stream, false);
    }
    return Http::FilterDataStatus::Continue;
}

bool GressFilter::recordBody(Buffer::OwnedImpl& body, Buffer::Instance& data,
                             bool end_stream, bool is_req) {
    auto& stream_info = is_req ? decoder_callbacks_->streamInfo() : encoder_callbacks_->streamInfo();
    std::string body_key = is_req ? "request_body" : "response_body";

    uint64_t logging_size = static_cast<uint64_t>(config_.max_logging_body_size_per_reqeuest());
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
