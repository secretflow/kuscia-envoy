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

#include <stdexcept>
#include <cstdint>
#include <arpa/inet.h>
#include <cstring>

#include "kuscia/source/filters/http/kuscia_common/framer.h"
#include "framer.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaCommon {


static const std::map<DecodeStatus, absl::string_view> decodeStatusMessageMap = {
    {DecodeStatus::NeedMoreData, "need more data"},
    {DecodeStatus::ErrorObjectTooLarge, "object too large"},
    {DecodeStatus::ErrorInvalidData, "invalid data"}
};

absl::string_view decodeStatusString(DecodeStatus status) {
    auto it = decodeStatusMessageMap.find(status);
    if (it != decodeStatusMessageMap.end()) {
        return it->second;
    } else {
        return "";
    }
}

DecodeStatus LengthDelimitedFrameReader::read(Buffer::Instance& input)
{
    if (remaining_ == 0) {
        uint32_t frameLength;
        if (!readByLen(input, sizeof(frameLength), len_frame_)) {
            return DecodeStatus::NeedMoreData;
        }

        std::memcpy(&frameLength, len_frame_.data(), sizeof(frameLength));
        remaining_ = ntohl(frameLength);

        if (remaining_ > maxBytes_) {
            return DecodeStatus::ErrorObjectTooLarge;
        }

        len_frame_.resize(0);
        data_frame_.resize(0);
    }

    if (!readByLen(input, remaining_, data_frame_)) {
        return DecodeStatus::NeedMoreData;
    }

    remaining_ = 0;
    return DecodeStatus::Ok;
}

bool LengthDelimitedFrameReader::readByLen(Buffer::Instance& input, size_t len, std::vector<uint8_t>& frame)
{
    size_t frame_size = frame.size();
    size_t input_len = input.length();

    if (frame_size + input_len < len) {
        ENVOY_LOG(info, "Need more input data, frame size: {} + input-len: {} < {}", frame_size, input_len, len);

        frame.resize(frame_size + input_len);
        input.copyOut(0, input_len, frame.data() + frame_size);
        input.drain(input_len);
        return false; // need more input data
    }

    frame.resize(len);
    input.copyOut(0, len - frame_size, frame.data() + frame_size);
    input.drain(len - frame_size);

    return true;
}

void KusciaCommon::LengthDelimitedFrameWriter::write(const char data[], uint32_t size, Buffer::OwnedImpl &output)
{
    uint32_t net_size = htonl(size);
    output.add(&net_size, sizeof(net_size));
    output.add(data, size);
}

} // namespace KusciaCommon
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
