#pragma once

#include "envoy/extensions/filters/http/priority_buffer/v3/priority_buffer.pb.h"
#include "envoy/extensions/filters/http/priority_buffer/v3/priority_buffer.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PriorityBufferFilter {

/**
 * Config registration for the adaptive concurrency limit filter. @see NamedHttpFilterConfigFactory.
 */
class PriorityBufferFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::priority_buffer::v3::FilterConfig> {
public:
  PriorityBufferFilterFactory() : FactoryBase("envoy.filters.http.priority_buffer") {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::priority_buffer::v3::FilterConfig& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace PriorityBufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
