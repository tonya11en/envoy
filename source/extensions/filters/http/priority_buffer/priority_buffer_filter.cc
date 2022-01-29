#include "source/extensions/filters/http/priority_buffer/priority_buffer_filter.h"

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/http/priority_buffer/v3/priority_buffer.pb.h"

#include "source/common/common/assert.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/runtime/runtime_impl.h"

constexpr absl::string_view kDefaultPriority{"default"};

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PriorityBufferFilter {

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::priority_buffer::v3::FilterConfig& proto_config,
    Runtime::Loader& runtime)
    : priority_header_key_(proto_config.header_key()), runtime_(runtime),
      proto_config_(proto_config) {}

PriorityBufferFilter::PriorityBufferFilter(std::shared_ptr<FilterConfig> config,
                                           ThreadLocal::TypedSlotPtr<ThreadLocalQueueImpl>&& tls)
    : config_(std::move(config)),
      priority_buffer_feature_(config->proto().enabled(), config->runtime()), tls_(std::move(tls)) {
}

Http::FilterHeadersStatus PriorityBufferFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                              bool) {
  if (dequeued_ || !priority_buffer_feature_.enabled()) {
    // This request is scheduled from the buffer queue.
    return Http::FilterHeadersStatus::Continue;
  }

  dequeued_ = true;
  auto pri = headers.get(config_->priorityHeaderKey());
  if (pri.empty()) {
    bufferQueue().enqueue(kDefaultPriority, callbacks_);
  } else {
    bufferQueue().enqueue(pri[0]->value().getStringView(), callbacks_);
  }

  return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
}

void PriorityBufferFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

} // namespace PriorityBufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
