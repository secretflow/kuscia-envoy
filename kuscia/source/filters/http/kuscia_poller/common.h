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

#include "kuscia/api/filters/http/kuscia_poller/v3/poller.pb.h"
#include "kuscia/api/filters/http/kuscia_receiver/v3/message.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaPoller {

using PollerConfigPbConfig = envoy::extensions::filters::http::kuscia_poller::v3::Poller;
using RequestMessagePb = envoy::extensions::filters::http::kuscia_receiver::v3::RequestMessage;
using ResponseMessagePb = envoy::extensions::filters::http::kuscia_receiver::v3::ResponseMessage;

static const std::string ReceiverService = "msg-receiver";

} // namespace KusciaPoller
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy