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


#pragma once

#include <unordered_map>
#include <memory>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "kuscia/api/filters/http/kuscia_gress/v3/gress.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaGress {

using GressPbConfig = envoy::extensions::filters::http::kuscia_gress::v3::Gress;

class GressFilter : public Envoy::Http::PassThroughFilter,
    public Logger::Loggable<Logger::Id::filter> {
  public:
    explicit GressFilter(const GressPbConfig& config) :
        config_(config),
        host_(),
        request_id_(),
        record_request_body_(false),
        record_response_body_(false) {}


    Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                            bool) override;
    Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

    Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                            bool end_stream) override;
    Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;

  private:
    bool recordBody(Buffer::OwnedImpl& body, Buffer::Instance& data, bool end_stream, bool is_req);

    GressPbConfig config_;
    std::string host_;
    std::string request_id_;

    bool record_request_body_;
    bool record_response_body_;
    Buffer::OwnedImpl req_body_;
    Buffer::OwnedImpl resp_body_;
};

} // namespace KusciaGress
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
