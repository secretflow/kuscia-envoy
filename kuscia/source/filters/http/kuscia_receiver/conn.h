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

#include "event.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/hex.h"
#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/network/connection_impl.h"
#include <arpa/inet.h>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <functional>
#include <http_parser.h>
#include <iterator>
#include <list>
#include <memory>
#include <queue>
#include <utility>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaReceiver {

#define ASSERT_STREAM_LOG_RETURN(cond)                                                            \
  do {                                                                                            \
    if (cond) {                                                                                   \
      ENVOY_LOG(trace, "[TcpConn] [C{}][S{}] before {} L{}, stream destroyed", conn_id_,          \
                stream_id_, __FUNCTION__, __LINE__);                                              \
      return;                                                                                     \
    } else {                                                                                      \
      ENVOY_LOG(trace, "[TcpConn] [C{}][S{}] before {} L{}, stream ok", conn_id_, stream_id_,     \
                __FUNCTION__, __LINE__);                                                          \
    }                                                                                             \
  } while (0)

#define ASSERT_STREAM_LOG_RETURN_VAL(cond, retval)                                                \
  do {                                                                                            \
    if (cond) {                                                                                   \
      ENVOY_LOG(trace, "[TcpConn] [C{}][S{}] before {} L{}, stream destroyed", conn_id_,          \
                stream_id_, __FUNCTION__, __LINE__);                                              \
      return retval;                                                                              \
    } else {                                                                                      \
      ENVOY_LOG(trace, "[TcpConn] [C{}][S{}] before {} L{}, stream ok", conn_id_, stream_id_,     \
                __FUNCTION__, __LINE__);                                                          \
    }                                                                                             \
  } while (0)

using BufferPtr = std::shared_ptr<Buffer::Instance>;

class TcpConn : public Logger::Loggable<Logger::Id::connection> {
public:
  explicit TcpConn(Http::StreamDecoderFilterCallbacks* cbs, StreamDestroyPtr sd)
      : cbs_(cbs), sd_(sd), delayed_(false) {
    if (sd_->load(std::memory_order_relaxed)) {
      delayed_ = true;
      return;
    }
    conn_id_ = cbs_->connection().ptr()->id();
    dispatcher_ = &cbs->dispatcher();
    stream_id_ = cbs->streamId();
    ENVOY_LOG(trace, "[TcpConn] [C{}][S{}] constructor, active tcp conn count {}", conn_id_,
              stream_id_, active_conn_.fetch_add(1, std::memory_order_relaxed) + 1);
  }

  ~TcpConn() {
    ENVOY_LOG(trace, "[TcpConn] [C{}][S{}] deconstructor, active tcp conn count {}", conn_id_,
              stream_id_, active_conn_.fetch_add(-1, std::memory_order_relaxed) - 1);
  }

  bool writeRequestHeader() {

    ASSERT_STREAM_LOG_RETURN_VAL(sd_->load(std::memory_order_relaxed), true);

    dispatcher_->post(
        [cbs = cbs_, sd = sd_, conn_id_ = conn_id_, stream_id_ = stream_id_]() mutable {
          ASSERT_STREAM_LOG_RETURN(sd->load(std::memory_order_relaxed));
          Http::ResponseHeaderMapPtr headers = Http::ResponseHeaderMapImpl::create();
          headers->setTransferEncoding(Http::Headers::get().TransferEncodingValues.Chunked);
          headers->setStatus(200);
          cbs->encodeHeaders(std::move(headers), false, REPLY_DETAIL);
        });

    ENVOY_LOG(trace, "[TcpConn] [C{}][S{}] write chunk header", conn_id_, stream_id_);
    return false;
  }

  bool writeRequestChunk(SendDataEventPtr event) {
    std::string str;
    if (!event->getReqData().SerializeToString(&str)) {
      ENVOY_LOG(error, "[TcpConn] [C{}][S{}] serialize request message failed", conn_id_,
                stream_id_);
      return false;
    }
    BufferPtr buffer = std::make_shared<Buffer::OwnedImpl>();
    uint32_t data_len = htonl(str.length());
    buffer->add(&data_len, sizeof(uint32_t));
    buffer->add(str);

    ASSERT_STREAM_LOG_RETURN_VAL(sd_->load(std::memory_order_relaxed), true);
    dispatcher_->post(
        [cbs = cbs_, buffer, sd = sd_, conn_id_ = conn_id_, stream_id_ = stream_id_] {
          ASSERT_STREAM_LOG_RETURN(sd->load(std::memory_order_relaxed));
          cbs->encodeData(*buffer, false);
        });

    ENVOY_LOG(trace, "[TcpConn] [C{}][S{}] write chunk data", conn_id_, stream_id_);
    return false;
  }

  void close() {
    // conn_->dispatcher().post(
    //     [conn = conn_] { conn->close(Network::ConnectionCloseType::FlushWriteAndDelay); });
    ENVOY_LOG(trace, "[TcpConn] [C{}][S{}] close tcp connection", conn_id_, stream_id_);
  }

  bool delayed() { return delayed_; }

  bool writeResponse(RecvDataEventPtr event) {
    auto& data = event->getRespData();
    auto hs = data.headers();
    bool end_stream = data.end_stream();
    bool is_chunked = data.chunk_data();
    int status_code = data.status_code();
    int index = data.index();
    ASSERT(end_stream || is_chunked);

    std::shared_ptr<Http::ResponseHeaderMapPtr> headers;
    if (hs.size() > 0 || status_code != 0) {
      headers =
          std::make_shared<Http::ResponseHeaderMapPtr>(Http::ResponseHeaderMapImpl::create());
      (*headers)->setStatus(status_code);
      for (auto iter = hs.begin(); iter != hs.end(); ++iter) {
        if (!iter->first.empty() && !iter->second.empty()) {
          (*headers)->setCopy(Envoy::Http::LowerCaseString(iter->first), iter->second);
        }
      }
    }

    BufferPtr buffer;
    if (data.body().size() > 0 || !headers) {
      buffer = std::make_shared<Buffer::OwnedImpl>();
      buffer->add(data.body());
    }

    ASSERT_STREAM_LOG_RETURN_VAL(sd_->load(std::memory_order_relaxed), true);

    dispatcher_->post([cbs = cbs_, headers = std::move(headers), buffer, end_stream, sd = sd_,
                       conn_id_ = conn_id_, stream_id_ = stream_id_] {
      ASSERT_STREAM_LOG_RETURN(sd->load(std::memory_order_relaxed));
      if (headers) {
        cbs->encodeHeaders(std::move(*headers), end_stream && !buffer, REPLY_DETAIL);
      }
      if (buffer) {
        cbs->encodeData(*buffer, end_stream);
      }
    });

    ENVOY_LOG(trace,
              "[TcpConn] [C{}][S{}] write response, status_code {} end_stream {} is_chunked {} "
              "index {} body size {}",
              conn_id_, stream_id_, status_code, end_stream, is_chunked, index,
              data.body().size());

    return end_stream;
  }

  void writeFailResponse(Http::Code code) {
    // ASSERT_STREAM_LOG_RETURN_VAL(sd_->load(std::memory_order_relaxed), true);
    dispatcher_->post([code = code, cbs = cbs_, sd = sd_, conn_id_ = conn_id_,
                       stream_id_ = stream_id_]() mutable {
      ASSERT_STREAM_LOG_RETURN(sd->load(std::memory_order_relaxed));
      Http::ResponseHeaderMapPtr headers = Http::ResponseHeaderMapImpl::create();
      headers->setStatus(static_cast<int>(code));
      cbs->encodeHeaders(std::move(headers), true, REPLY_DETAIL);
    });

    ENVOY_LOG(trace, "[TcpConn] [C{}][S{}] write fail response", conn_id_, stream_id_);
  }

protected:
  Http::StreamDecoderFilterCallbacks* cbs_;
  Event::Dispatcher* dispatcher_;
  StreamDestroyPtr sd_;
  uint64_t stream_id_;
  uint64_t conn_id_;
  bool delayed_;

private:
  static std::atomic<int> active_conn_;
};

std::atomic<int> TcpConn::active_conn_ = 0;

using TcpConnPtr = std::shared_ptr<TcpConn>;

class BufferedConn : public TcpConn {
public:
  explicit BufferedConn(Http::StreamDecoderFilterCallbacks* cbs, StreamDestroyPtr sd,
                        const std::string& uuid)
      : TcpConn(cbs, sd), writeable_(true), uuid_(uuid) {
    if (!delayed_) {
      auto conn = const_cast<Network::Connection*>(cbs_->connection().ptr());
      conn->setBufferLimits(buffer_limit_);
    }
    ENVOY_LOG(trace, "[BufferedConn] [C{}][S{}] constructor, active buffered conn count {}",
              conn_id_, stream_id_, active_conn_.fetch_add(1, std::memory_order_relaxed) + 1);
  }

  ~BufferedConn() {
    ENVOY_LOG(trace, "[BufferedConn] [C{}][S{}] deconstructor, active buffered conn count {}",
              conn_id_, stream_id_, active_conn_.fetch_add(-1, std::memory_order_relaxed) - 1);
  }

  bool isWriteable() const { return writeable_; }

  void setWriteable(bool writable) { writeable_ = writable; }

  std::list<ReceiverEventPtr> takeEvents() { return std::move(events_); }

  bool empty() const { return events_.empty(); }

  size_t size() const { return events_.size(); }

  void addEvent(const SendDataEventPtr event) { events_.emplace_back(event); }

  std::string& getUuid() { return uuid_; }

private:
  std::list<ReceiverEventPtr> events_;
  volatile bool writeable_;
  std::string uuid_;
  static std::atomic<int> active_conn_;
  constexpr static int buffer_limit_ = 1024 * 1024 * 100;
};

std::atomic<int> BufferedConn::active_conn_ = 0;

using BufferedConnPtr = std::shared_ptr<BufferedConn>;

class EventGreater {
public:
  bool operator()(const RecvDataEventPtr& lhs, const RecvDataEventPtr& rhs) const {
    return lhs->getRespData().index() > rhs->getRespData().index();
  }
};

class IndexedConn : public TcpConn {
public:
  explicit IndexedConn(Http::StreamDecoderFilterCallbacks* cbs, StreamDestroyPtr sd)
      : TcpConn(cbs, sd), events_(EventGreater()), index_(0) {
    ENVOY_LOG(trace, "[IndexedConn] [C{}][S{}] constructor, active indexed conn count {}",
              conn_id_, stream_id_, active_conn_.fetch_add(1, std::memory_order_relaxed) + 1);
  }

  ~IndexedConn() {
    ENVOY_LOG(trace, "[IndexedConn] [C{}][S{}] deconstructor, active indexed conn count {}",
              conn_id_, stream_id_, active_conn_.fetch_add(-1, std::memory_order_relaxed) - 1);
  }

  bool writeResponseIndexed(RecvDataEventPtr event) {
    auto& data = event->getRespData();
    if (data.index() == index_) {
      if (writeResponse(event)) {
        return true;
      }
      ++index_;
    } else {
      events_.push(event);
      ENVOY_LOG(trace, "[IndexedConn] [C{}][S{}] push event to queue, index {}, queue size {}",
                conn_id_, stream_id_, data.index(), events_.size());
      return false;
    }
    while (!events_.empty() && events_.top()->getRespData().index() == index_) {
      auto event = events_.top();
      events_.pop();
      if (writeResponse(event)) {
        return true;
      }
      ++index_;
    }
    return false;
  }

private:
  std::priority_queue<RecvDataEventPtr, std::vector<RecvDataEventPtr>, EventGreater> events_;
  static std::atomic<int> active_conn_;
  int index_;
};

std::atomic<int> IndexedConn::active_conn_ = 0;

using IndexedConnPtr = std::shared_ptr<IndexedConn>;

} // namespace KusciaReceiver
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
