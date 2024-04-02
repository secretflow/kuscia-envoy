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

#include <vector>
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaCommon {

enum class DecodeStatus {
    Ok,
    NeedMoreData,
    ErrorObjectTooLarge,
    ErrorInvalidData,
};

extern absl::string_view  decodeStatusString(DecodeStatus status);

class LengthDelimitedFrameReader : public Logger::Loggable<Logger::Id::http> {
public:
    LengthDelimitedFrameReader() : remaining_(0), maxBytes_(16 * 1024 * 1024) {}

    DecodeStatus read(Buffer::Instance& input);

    const std::vector<uint8_t>& getDataFrame() const { return data_frame_; };

private:
    bool readByLen(Buffer::Instance& input, size_t len, std::vector<uint8_t>& frame);

    size_t remaining_;
    const size_t maxBytes_;
    std::vector<uint8_t> len_frame_;
    std::vector<uint8_t> data_frame_;
};

class LengthDelimitedFrameWriter {
public:
    void write(const char data[], uint32_t size, Buffer::OwnedImpl& output);
};

} // namespace KusciaCommon
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy