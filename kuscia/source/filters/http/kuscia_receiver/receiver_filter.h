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

#include "envoy/http/filter.h"
#include "envoy/network/connection.h"
#include "event.h"
#include "kuscia/api/filters/http/kuscia_receiver/v3/message.pb.h"
#include "source/common/common/logger.h"
#include "source/common/common/random_generator.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include <memory>
#include <string>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaReceiver {

static std::string genKey(absl::string_view src, absl::string_view dst) {
  return absl::StrCat(src, "/", dst);
}

class ReceiverFilterConfig {
public:
  explicit ReceiverFilterConfig(const ReceiverPbConfig& config,
                                Server::Configuration::FactoryContext& context)
      : context_(context) {
    namespace_ = config.self_namespace();
    for (auto& rule : config.rules()) {
      auto key = genKey(rule.source(), rule.destination());
      rules_.emplace(std::move(key), true);
    }
  }

  bool hasRule(absl::string_view source, absl::string_view dest) const {
    return rules_.find(genKey(source, dest)) != rules_.end();
  }

  absl::string_view selfNamespace() const { return namespace_; }

private:
  Server::Configuration::FactoryContext& context_;
  std::map<std::string, bool> rules_;
  std::string namespace_;
};

using ReceiverFilterConfigSharedPtr = std::shared_ptr<ReceiverFilterConfig>;

class ReceiverFilter : public Http::PassThroughFilter, Logger::Loggable<Logger::Id::filter> {
public:
  explicit ReceiverFilter(ReceiverFilterConfigSharedPtr config);

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override;
  void onDestroy() override;

private:
  bool isPollRequest(Http::RequestHeaderMap& headers);
  bool isForwardRequest(Http::RequestHeaderMap& headers);
  bool isForwardResponse(Http::RequestHeaderMap& headers);

  void postEvent();
  void replyDirectly(Http::Code code);
  void registerConnTimeout();

private:
  const ReceiverFilterConfigSharedPtr config_;
  Random::RandomGeneratorImpl random_;
  RequestMessagePb request_pb_;
  std::string request_id_;
  std::string conn_uuid_;
  ReceiverEventType event_type_;
  Buffer::OwnedImpl body_;
  ReceiverRulePtr rule_;
  StreamDestroyPtr sd_;
  int timeout_sec_{0};
};

} // namespace KusciaReceiver
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy