#include "source/extensions/filters/http/priority_buffer/config.h"
#include "source/extensions/filters/http/priority_buffer/priority_buffer_filter.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/filters/http/priority_buffer/v3/priority_buffer.pb.h"
#include "envoy/extensions/filters/http/priority_buffer/v3/priority_buffer.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/priority_buffer/buffer_queue.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PriorityBufferFilter {

Http::FilterFactoryCb PriorityBufferFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::priority_buffer::v3::FilterConfig& config,
    const std::string&, Server::Configuration::FactoryContext& context) {

  auto tlq_cfg = std::make_shared<ThreadLocalQueueConfig>(config.thread_local_queue_config(),
                                                          context.mainThreadDispatcher());

  auto tlq = std::make_shared<ThreadLocalQueueImpl>(tlq_cfg, context.timeSource());
  tlq->start();

  auto filter_config = std::make_shared<FilterConfig>(config, context.runtime());

  return [fc = filter_config,
          tlq = std::move(tlq)](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<PriorityBufferFilter>(fc, tlq));
  };
}

/**
 * Static registration for the admission_control filter. @see RegisterFactory.
 */
REGISTER_FACTORY(PriorityBufferFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace PriorityBufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
