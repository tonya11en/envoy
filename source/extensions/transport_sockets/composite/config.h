#pragma once

#include "envoy/server/transport_socket_config.h"
#include "source/extensions/transport_sockets/common/passthrough.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Composite {

class CompositeTransportSocketFactory : public Network::UpstreamTransportSocketFactory {
public:
  CompositeTransportSocketFactory(
      Network::UpstreamTransportSocketFactoryPtr outer_factory,
      Network::UpstreamTransportSocketFactoryPtr inner_factory);

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        std::shared_ptr<const Upstream::HostDescription> host) const override;

  void hashKey(std::vector<uint8_t>& key,
               Network::TransportSocketOptionsConstSharedPtr options) const override;
  
  bool implementsSecureTransport() const override;
  absl::string_view defaultServerNameIndication() const override;

private:
  Network::UpstreamTransportSocketFactoryPtr outer_factory_;
  Network::UpstreamTransportSocketFactoryPtr inner_factory_;
};

class CompositeTransportSocketConfigFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory {
public:
  // Server::Configuration::UpstreamTransportSocketConfigFactory
  absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr> createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override { return "envoy.transport_sockets.composite"; }
};

} // namespace Composite
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
