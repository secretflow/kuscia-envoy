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

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "kuscia/api/filters/http/kuscia_crypt/v3/crypt.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaCrypt {

class CryptFilterTest;
class KusciaCrypter;

using KusciaCrypterSharedPtr = std::shared_ptr<KusciaCrypter>;
using CryptRule = envoy::extensions::filters::http::kuscia_crypt::v3::CryptRule;


class KusciaCrypter : public Logger::Loggable<Logger::Id::filter> {
  public:
    static KusciaCrypterSharedPtr createForwardCrypter(const CryptRule& rule,
                                                       Http::RequestHeaderMap& headers);
    static KusciaCrypterSharedPtr createReverseCrypter(const CryptRule& rule,
                                                       Http::RequestHeaderMap& headers);

    KusciaCrypter(const std::string& secret_key, const std::string& version) :
        secret_key_(secret_key),
        version_(version) {}

    KusciaCrypter(std::string&& secret_key, std::string&& version) :
        secret_key_(std::move(secret_key)),
        version_(std::move(version)) {}

    virtual ~KusciaCrypter() {}
    virtual bool encrypt(Buffer::Instance& data, bool end_stream, Buffer::Instance& left_data) = 0;
    virtual bool decrypt(Buffer::Instance& data, bool end_stream, Buffer::Instance& left_data) = 0;

    virtual bool checkRespEncrypt(Http::ResponseHeaderMap& headers) = 0;
    virtual bool checkRespDecrypt(Http::ResponseHeaderMap& headers) = 0;

  protected:
    const std::string secret_key_;
    const std::string version_;

    friend class CryptFilterTest;
};

} // namespace KusciaCrypt
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

