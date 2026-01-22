#include "source/extensions/transport_sockets/composite/config.h"

#include "envoy/extensions/transport_sockets/composite/v3/composite.pb.h"
#include "envoy/extensions/transport_sockets/composite/v3/composite.pb.validate.h"

#include "source/common/config/utility.h"

#include "source/extensions/transport_sockets/composite/composite.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Composite {

CompositeTransportSocketFactory::CompositeTransportSocketFactory(
    Network::UpstreamTransportSocketFactoryPtr outer_factory,
    Network::UpstreamTransportSocketFactoryPtr inner_factory)
    : outer_factory_(std::move(outer_factory)), inner_factory_(std::move(inner_factory)) {}

Network::TransportSocketPtr CompositeTransportSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsConstSharedPtr options,
    std::shared_ptr<const Upstream::HostDescription> host) const {
  auto outer_socket = outer_factory_->createTransportSocket(options, host);
  auto inner_socket = inner_factory_->createTransportSocket(options, host);
  return std::make_unique<CompositeTransportSocket>(std::move(outer_socket), std::move(inner_socket));
}

void CompositeTransportSocketFactory::hashKey(
    std::vector<uint8_t>& key, Network::TransportSocketOptionsConstSharedPtr options) const {
  outer_factory_->hashKey(key, options);
  inner_factory_->hashKey(key, options);
}

bool CompositeTransportSocketFactory::implementsSecureTransport() const {
  return outer_factory_->implementsSecureTransport() || inner_factory_->implementsSecureTransport();
}

absl::string_view CompositeTransportSocketFactory::defaultServerNameIndication() const {
  if (!outer_factory_->defaultServerNameIndication().empty()) {
    return outer_factory_->defaultServerNameIndication();
  }
  return inner_factory_->defaultServerNameIndication();
}

absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
CompositeTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config,
    Server::Configuration::TransportSocketFactoryContext& context) {
  const auto& composite_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::transport_sockets::composite::v3::CompositeTransportSocket&>(
          config, context.messageValidationVisitor());

  // Load Outer Socket Factory
  auto& outer_transport_socket = composite_config.outer_transport_socket();
  auto& outer_factory = Config::Utility::getAndCheckFactory<Server::Configuration::UpstreamTransportSocketConfigFactory>(
      outer_transport_socket);
  ProtobufTypes::MessagePtr outer_factory_config = Config::Utility::translateToFactoryConfig(
      outer_transport_socket, context.messageValidationVisitor(), outer_factory);
  
  auto outer_socket_factory = outer_factory.createTransportSocketFactory(
      *outer_factory_config, context);
  if (!outer_socket_factory.ok()) {
      return outer_socket_factory.status();
  }

  // Load Inner Socket Factory
  auto& inner_transport_socket = composite_config.inner_transport_socket();
  auto& inner_factory = Config::Utility::getAndCheckFactory<Server::Configuration::UpstreamTransportSocketConfigFactory>(
        inner_transport_socket);
  ProtobufTypes::MessagePtr inner_factory_config = Config::Utility::translateToFactoryConfig(
      inner_transport_socket, context.messageValidationVisitor(), inner_factory);
    
  auto inner_socket_factory = inner_factory.createTransportSocketFactory(
        *inner_factory_config, context);
  if (!inner_socket_factory.ok()) {
        return inner_socket_factory.status();
  }

  return std::make_unique<CompositeTransportSocketFactory>(
      std::move(outer_socket_factory.value()), std::move(inner_socket_factory.value()));
}

ProtobufTypes::MessagePtr CompositeTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::composite::v3::CompositeTransportSocket>();
}

REGISTER_FACTORY(CompositeTransportSocketConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace Composite
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
