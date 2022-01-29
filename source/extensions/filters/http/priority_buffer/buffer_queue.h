#pragma once

#include <chrono>
#include <vector>
#include <queue>
#include <utility>

#include "absl/synchronization/mutex.h"

#include "envoy/common/pure.h"
#include "envoy/http/filter.h"
#include "envoy/common/time.h"
#include "envoy/http/codes.h"
#include "envoy/thread_local/thread_local_object.h"
#include "source/common/common/utility.h"
#include "source/common/protobuf/utility.h"
#include "envoy/extensions/filters/http/priority_buffer/v3/priority_buffer.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PriorityBufferFilter {

class BufferQueue {
public:
  virtual ~BufferQueue() = default;

  /**
   * Starts the scheduler.
   */
  virtual void start() PURE;

  virtual void enqueue(absl::string_view priority, Http::StreamDecoderFilterCallbacks* cb) PURE;
};

class ThreadLocalQueueConfig {
public:
  using Config =
      envoy::extensions::filters::http::priority_buffer::v3::ThreadLocalBufferQueueConfig;
  ThreadLocalQueueConfig(const Config& proto_config, Event::Dispatcher& dispatcher)
      : dispatcher_(dispatcher),
        buffering_interval_(PROTOBUF_GET_MS_REQUIRED(proto_config, buffering_interval)),
        queue_timeout_(PROTOBUF_GET_MS_REQUIRED(proto_config, queue_timeout)) {
    if (proto_config.priority_order_size() == 0) {
      priority_ordering_.emplace_back("default");
    } else {
      priority_ordering_.reserve(proto_config.priority_order_size());
      for (int ii = 0; ii < proto_config.priority_order_size(); ++ii) {
        priority_ordering_.emplace_back(proto_config.priority_order(ii));
      }
    }
  }

  std::chrono::milliseconds bufferingInterval() const { return buffering_interval_; }

  const std::vector<Http::LowerCaseString>& priorityOrder() const { return priority_ordering_; }

  Event::Dispatcher& dispatcher() const { return dispatcher_; }

  std::chrono::nanoseconds queueTimeout() const { return queue_timeout_; }

private:
  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds buffering_interval_;
  std::vector<Http::LowerCaseString> priority_ordering_;
  std::chrono::nanoseconds queue_timeout_;
};

class ThreadLocalQueueImpl : public BufferQueue, public ThreadLocal::ThreadLocalObject {
  // There's no real reason for this to be a queue, so we can cut down on memory allocations by just
  // using a vector.
  using CallbackQueue =
      std::vector<std::pair<Http::StreamDecoderFilterCallbacks*, Envoy::MonotonicTime>>;

public:
  ThreadLocalQueueImpl(std::shared_ptr<ThreadLocalQueueConfig> config, TimeSource& time_source)
      : config_(std::move(config)), time_source_(time_source) {
    for (const auto& priority : config_->priorityOrder()) {
      auto entry = std::make_unique<PriorityQueueEntry>();
      entry->priority_ = priority;
      priority_queues_.emplace_back(std::move(entry));
    }
  }

  // BufferQueue
  void start() override;
  void enqueue(absl::string_view priority, Http::StreamDecoderFilterCallbacks* cb) override;

private:
  // Flushes queued requests in priority order to be scheduled by the dispatcher.
  void flush();

  using PriorityQueueEntry = struct {
    absl::string_view priority_;
    CallbackQueue queue_ ABSL_GUARDED_BY(qmtx_);
    absl::Mutex qmtx_;
  };

  // Assuming the number of priorities is small, so we can just do a linear search on this vector
  // instead of using a map.
  std::vector<std::unique_ptr<PriorityQueueEntry>> priority_queues_;

  std::shared_ptr<ThreadLocalQueueConfig> config_;
  TimeSource& time_source_;
  bool started_{false};

  Event::TimerPtr flush_timer_;
};

} // namespace PriorityBufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
