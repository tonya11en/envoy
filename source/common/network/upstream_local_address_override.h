#pragma once

#include <string>

#include "absl/types/optional.h"

#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Upstream {

struct UpstreamBindAddressOverride : public StreamInfo::FilterState::Object {
  absl::optional<std::string> addr_port;
  absl::optional<std::string> network_namespace;

  static absl::string_view key() { return "envoy.upstream.upstream_bind_address_override"; }
};

} // namespace Upstream
} // namespace Envoy
