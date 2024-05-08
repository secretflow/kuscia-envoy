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

#include "kuscia/source/filters/http/kuscia_common/coder.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaCommon {

DecodeStatus KusciaCommon::Decoder::decode(Envoy::Buffer::Instance& data, google::protobuf::Message &message)
{
    DecodeStatus status = frameReader_.read(data);
    if (status != DecodeStatus::Ok) {
        return status;
    }

    auto data_frame = frameReader_.getDataFrame();

    if (!message.ParseFromArray(data_frame.data(), data_frame.size())) {
        return DecodeStatus::ErrorInvalidData;
    }

    return DecodeStatus::Ok;
}

} // namespace KusciaCommon
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
