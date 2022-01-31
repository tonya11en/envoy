#include "source/extensions/filters/http/priority_buffer/buffer_queue.h"

#include "envoy/event/dispatcher.h"
#include "source/common/common/assert.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PriorityBufferFilter {

void ThreadLocalQueueImpl::start() {
  ASSERT(!started_);

  flush_timer_ = config_->dispatcher().createTimer([this]() -> void {
    std::cout << "@tallen timer fired\n";
    this->flush();
    flush_timer_->enableTimer(config_->bufferingInterval());
  });
  flush_timer_->enableTimer(config_->bufferingInterval());

  time_source_.monotonicTime();
  started_ = true;
  // todo
}

void ThreadLocalQueueImpl::enqueue(absl::string_view priority,
                                   Http::StreamDecoderFilterCallbacks* cb) {
  std::cout << "@tallen enqueue with priority " << priority << std::endl;
  ASSERT(started_);
  for (auto& entry : priority_queues_) {
    std::cout << "@tallen looping - " << entry->priority_ << std::endl;
    if (StringUtil::CaseInsensitiveCompare()(priority, entry->priority_)) {
      absl::MutexLock ml(&entry->qmtx_);
      std::cout << "@tallen matched - " << entry->priority_ << std::endl;
      entry->queue_.emplace_back(cb, time_source_.monotonicTime());
      return;
    }
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

void ThreadLocalQueueImpl::flush() {
  std::cout << "@tallen FLUSH called\n";
  CallbackQueue q;

  for (auto& entry : priority_queues_) {
    absl::MutexLock ml(&entry->qmtx_);
    for (auto& p : entry->queue_) {
      auto cb = p.first;
      auto enqueue_time = p.second;
      ASSERT(cb != nullptr);

      auto elapsed_time = time_source_.monotonicTime() - enqueue_time;
      const bool expired = this->config_->queueTimeout() <= elapsed_time;
      if (expired) {
        std::cout << "expired - elapsed:" << elapsed_time.count()
                  << ", timeout=" << this->config_->queueTimeout().count() << std::endl;
        cb->dispatcher().post([cb]() {
          cb->sendLocalReply(Http::Code::ServiceUnavailable, "priority queue timeout", nullptr,
                             absl::nullopt, "priority_queue_timeout");
        });
        continue;
      }

      cb->dispatcher().post([cb]() { cb->continueDecoding(); });
    }
    entry->queue_.clear();
  }
}

} // namespace PriorityBufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
