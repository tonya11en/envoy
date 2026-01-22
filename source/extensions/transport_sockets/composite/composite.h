#pragma once

#include "envoy/network/transport_socket.h"
#include "source/common/network/io_socket_handle_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Composite {

class CompositeTransportSocket : public Network::TransportSocket {
public:
  CompositeTransportSocket(Network::TransportSocketPtr outer_socket,
                           Network::TransportSocketPtr inner_socket);

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  absl::string_view failureReason() const override;
  bool canFlushClose() override;
  void closeSocket(Network::ConnectionEvent event) override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  void onConnected() override;
  Ssl::ConnectionInfoConstSharedPtr ssl() const override;
  bool startSecureTransport() override;
  void configureInitialCongestionWindow(uint64_t bandwidth_bits_per_sec,
                                        std::chrono::microseconds rtt) override;

private:
  Network::TransportSocketPtr outer_socket_;
  Network::TransportSocketPtr inner_socket_;
  std::unique_ptr<Network::TransportSocketCallbacks> inner_callbacks_;
};

} // namespace Composite
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
