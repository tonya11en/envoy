#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/server/filter_config.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/common/runtime/runtime_protos.h"
#include "envoy/stats/stats_macros.h"
#include "source/common/buffer/buffer_impl.h"

#include "source/extensions/filters/http/priority_buffer/buffer_queue.h"
#include "envoy/extensions/filters/http/priority_buffer/v3/priority_buffer.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PriorityBufferFilter {

class FilterConfig {
public:
  FilterConfig(
      const envoy::extensions::filters::http::priority_buffer::v3::FilterConfig& proto_config,
      Runtime::Loader& runtime);

  Http::LowerCaseString priorityHeaderKey() const { return priority_header_key_; }
  Runtime::Loader& runtime() const { return runtime_; }
  const envoy::extensions::filters::http::priority_buffer::v3::FilterConfig& proto() {
    return proto_config_;
  }

private:
  Http::LowerCaseString priority_header_key_;
  Runtime::Loader& runtime_;
  const envoy::extensions::filters::http::priority_buffer::v3::FilterConfig& proto_config_;
};

using FilterConfigPtr = std::shared_ptr<FilterConfig>;

using ConfigProto = envoy::extensions::filters::http::priority_buffer::v3::FilterConfig;

/**
 * A filter that is capable of buffering an entire request before dispatching it upstream.
 */
class PriorityBufferFilter : public Http::PassThroughFilter, Logger::Loggable<Logger::Id::filter> {
public:
  PriorityBufferFilter(std::shared_ptr<FilterConfig> config,
                       ThreadLocal::TypedSlotPtr<ThreadLocalQueueImpl>&& tls);

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  ThreadLocalQueueImpl& bufferQueue() { return **tls_; }

private:
  FilterConfigPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};

  // Whether this request has been queued already.
  bool dequeued_{false};

  Runtime::FeatureFlag priority_buffer_feature_;
  const ThreadLocal::TypedSlotPtr<ThreadLocalQueueImpl> tls_;
};

} // namespace PriorityBufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
