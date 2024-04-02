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

#include "conn.h"
#include "envoy/http/async_client.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/http/message.h"
#include "envoy/upstream/cluster_manager.h"
#include "event.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include <condition_variable>
#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KusciaReceiver {

static std::string rule2str(ReceiverRulePtr rule) {
  return absl::StrCat(rule->source(), "/", rule->destination(), "/", rule->service());
}

static bool ruleMatch(ReceiverRulePtr x, ReceiverRulePtr y) {
  return x->source() == y->source() && x->destination() == y->destination() &&
         x->service() == y->service();
}

constexpr static int queue_capacity = 100000;
constexpr static int miss_capacity = 2000;

class EventLoopSingleton : Logger::Loggable<Logger::Id::filter> {
public:
  EventLoopSingleton(const EventLoopSingleton&) = delete;
  EventLoopSingleton(const EventLoopSingleton&&) = delete;
  EventLoopSingleton& operator=(const EventLoopSingleton&) = delete;

  static EventLoopSingleton& GetInstance() {
    static EventLoopSingleton singleton(queue_capacity);
    return singleton;
  }

  void postEvent(ReceiverEventPtr event) {
    bool full = false;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      full_cv_.wait(lock, [this] { return events_.size() < capacity_ || stop_; });
      if (stop_) {
        return;
      }
      events_.push_back(event);
      if (events_.size() >= capacity_) {
        full = true;
      }
    }
    empty_cv_.notify_one();
    if (full) {
      ENVOY_LOG(warn, "[EventLoopSingleton] event queue is full!");
    }
  }

private:
  explicit EventLoopSingleton(int capacity) : capacity_(capacity), stop_(false) {
    std::thread([this]() { run(); }).detach();
  }

  ~EventLoopSingleton() { stop(); }

  void run() {
    while (!stop_) {
      std::shared_ptr<ReceiverEvent> event;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        empty_cv_.wait(lock, [this] { return !events_.empty() || stop_; });
        if (stop_) {
          return;
        }
        event = std::move(events_.front());
        events_.pop_front();
      }
      full_cv_.notify_one();
      dispatch(std::move(event));
    }
  }

  void stop() {
    stop_ = true;
    empty_cv_.notify_one();
    full_cv_.notify_all();
  }

  void dispatch(ReceiverEventPtr event) {
    switch (event->getEventType()) {
    case ReceiverEventType::RECEIVER_EVENT_TYPE_CONNECT:
      registerConn(std::static_pointer_cast<ConnectEvent>(event));
      break;
    case ReceiverEventType::RECEIVER_EVENT_TYPE_DISCONNECT:
      unregisterConn(std::static_pointer_cast<DisconnectEvent>(event));
      break;
    case ReceiverEventType::RECEIVER_EVENT_TYPE_DATA_SEND:
      sendData(std::static_pointer_cast<SendDataEvent>(event));
      break;
    case ReceiverEventType::RECEIVER_EVENT_TYPE_DATA_RECV:
      recvData(std::static_pointer_cast<RecvDataEvent>(event));
      break;
    case ReceiverEventType::RECEIVER_EVENT_TYPE_BUFFER_HIGH:
      stopConnWrite(std::static_pointer_cast<BufferHighEvent>(event));
      break;
    case ReceiverEventType::RECEIVER_EVENT_TYPE_BUFFER_LOW:
      resumeConnWrite(std::static_pointer_cast<BufferLowEvent>(event));
      break;
    case ReceiverEventType::RECEIVER_EVENT_TYPE_TIMEOUT:
      timeoutConn(std::static_pointer_cast<TimeoutEvent>(event));
      break;
    case ReceiverEventType::RECEIVER_EVENT_TYPE_CLOSE:
      closeConn(std::static_pointer_cast<CloseEvent>(event));
      break;
    default:
      break;
    }
  }

  void registerConn(ConnectEventPtr event) {
    ENVOY_LOG(info, "[EventLoopSingleton] register connection {}", rule2str(event->getRule()));
    auto rule = event->getRule();
    ASSERT(event->getCbs() != nullptr);
    ASSERT(rule != nullptr);

    auto conn = std::make_shared<BufferedConn>(event->getCbs(), event->getStreamDestroyed(),
                                               event->getUuid());
    if (conn->delayed()) {
      ENVOY_LOG(warn, "[EventLoopSingleton] delayed poll connection {}", rule2str(rule));
      return;
    }
    auto key = rule2str(rule);
    if (!conn->writeRequestHeader()) {
      poll_conns_[key] = std::move(conn);
    }
    if (!miss_events_.empty()) {
      ENVOY_LOG(warn, "[EventLoopSingleton] miss events size {}", miss_events_.size());
      requeueMissEvents(rule);
    }
    // curlRegister(rule, false);
  }

  void requeueMissEvents(ReceiverRulePtr rule) {
    for (auto iter = miss_events_.begin(); iter != miss_events_.end();) {
      auto event = *iter;
      if (event->getStreamDestroyed()->load(std::memory_order_relaxed)) {
        miss_events_.erase(iter++);
        continue;
      }
      if (ruleMatch(event->getRule(), rule)) {
        ENVOY_LOG(warn, "[EventLoopSingleton] requeue miss event, key {} request_id {}",
                  rule2str(rule), event->getReqData().id());
        miss_events_.erase(iter++);
        sendData(event);
        continue;
      }
      ++iter;
    }
  }

  void unregisterConn(DisconnectEventPtr event) {
    ENVOY_LOG(info, "[EventLoopSingleton] unregister connection {}", rule2str(event->getRule()));
    auto rule = event->getRule();
    ASSERT(rule != nullptr);

    auto iter = poll_conns_.find(rule2str(rule));
    if (iter != poll_conns_.end()) {
      if (event->getUuid() == iter->second->getUuid()) {
        poll_conns_.erase(iter);
      }
    }
    // curlUnregister(rule, false);
  }

  void timeoutConn(TimeoutEventPtr event) {
    ENVOY_LOG(info, "[EventLoopSingleton] timeout connection {}", rule2str(event->getRule()));
    auto rule = event->getRule();
    ASSERT(rule != nullptr);

    auto key = rule2str(rule);
    auto iter = poll_conns_.find(key);
    if (iter != poll_conns_.end()) {
      if (event->getUuid() == iter->second->getUuid()) {
        iter->second->close();
      }
    }
  }

  void stopConnWrite(BufferHighEventPtr event) {
    ENVOY_LOG(info, "[EventLoopSingleton] stop write connection {}", rule2str(event->getRule()));
    auto rule = event->getRule();
    ASSERT(rule != nullptr);

    auto key = rule2str(rule);
    auto iter = poll_conns_.find(key);
    if (iter == poll_conns_.end()) {
      return;
    }
    auto conn = iter->second;
    conn->setWriteable(false);
  }

  void resumeConnWrite(BufferLowEventPtr event) {
    ENVOY_LOG(info, "[EventLoopSingleton] resume write connection {}", rule2str(event->getRule()));
    auto rule = event->getRule();
    ASSERT(rule != nullptr);

    auto key = rule2str(rule);
    auto iter = poll_conns_.find(key);
    if (iter == poll_conns_.end()) {
      return;
    }
    auto conn = iter->second;
    conn->setWriteable(true);
    if (!conn->empty()) {
      events_.splice(events_.begin(), conn->takeEvents());
    }
  }

  void sendData(SendDataEventPtr event) {
    ENVOY_LOG(trace, "[EventLoopSingleton] send data to connection {}",
              rule2str(event->getRule()));
    auto rule = event->getRule();
    ASSERT(event->getCbs() != nullptr);
    ASSERT(rule != nullptr);

    auto key = rule2str(rule);
    auto iter = poll_conns_.find(key);
    if (iter == poll_conns_.end()) {
      ENVOY_LOG(warn, "[EventLoopSingleton] cannot find poll connection, key {}", key);
      while (miss_events_.size() >= miss_capacity) {
        replyMissEvents(std::move(miss_events_.front()));
        miss_events_.pop_front();
      }
      miss_events_.emplace_back(std::move(event));
      return;
    }

    auto& data = event->getReqData();
    std::string request_id = data.id();
    auto recv_conn = std::make_shared<IndexedConn>(event->getCbs(), event->getStreamDestroyed());
    if (recv_conn->delayed()) {
      ENVOY_LOG(warn, "[EventLoopSingleton] delayed recv conn, request_id {}", request_id);
      return;
    }

    // keep <request_id, recv_conn> record
    recv_conns_[request_id] = std::move(recv_conn);
    ENVOY_LOG(trace, "[EventLoopSingleton] add recv conn pair {}", request_id);

    // send data through poll conn, store the event in the queue if conn not writable
    auto poll_conn = iter->second;
    if (!poll_conn->isWriteable()) {
      poll_conn->addEvent(std::move(event));
      return;
    }
    poll_conn->writeRequestChunk(std::move(event));
  }

  void replyMissEvents(SendDataEventPtr event) {
    auto request_id = event->getReqData().id();
    ENVOY_LOG(warn, "[EventLoopSingleton] reply miss events, key {} request_id {}",
              rule2str(event->getRule()), request_id);
    auto conn = std::make_shared<IndexedConn>(event->getCbs(), event->getStreamDestroyed());
    if (conn->delayed()) {
      ENVOY_LOG(warn, "[EventLoopSingleton] delayed recv conn, request_id {}", request_id);
      return;
    }
    conn->writeFailResponse(Http::Code::ServiceUnavailable);
  }

  void recvData(RecvDataEventPtr event) {
    ENVOY_LOG(trace, "[EventLoopSingleton] recv data from connection {}", event->getRequestId());
    auto request_id = event->getRequestId();
    ASSERT(!request_id.empty());

    auto iter = recv_conns_.find(request_id);
    if (iter == recv_conns_.end()) {
      ENVOY_LOG(warn, "[EventLoopSingleton] cannot find recv connection, request_id {}",
                request_id);
      return;
    }
    ENVOY_LOG(trace, "[EventLoopSingleton] recv connection found, request_id {}", request_id);
    iter->second->writeResponseIndexed(event);
  }

  void closeConn(CloseEventPtr event) {
    ENVOY_LOG(trace, "[EventLoopSingleton] close connection {}", event->getRequestId());
    recv_conns_.erase(event->getRequestId());
  }

private:
  std::unordered_map<std::string, BufferedConnPtr> poll_conns_;
  std::unordered_map<std::string, IndexedConnPtr> recv_conns_;
  std::list<SendDataEventPtr> miss_events_;
  std::list<ReceiverEventPtr> events_;
  std::condition_variable empty_cv_;
  std::condition_variable full_cv_;
  std::mutex mutex_;
  size_t capacity_;
  bool stop_;
};

} // namespace KusciaReceiver
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
