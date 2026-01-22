#include "source/extensions/transport_sockets/composite/composite.h"

#include "envoy/buffer/buffer.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/io_socket_error_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Composite {

namespace {

// SimpleIoError implementation for wrapping IoErrorCode when we don't have a system errno
class SimpleIoError : public Api::IoError {
public:
  SimpleIoError(IoErrorCode code) : code_(code) {}
  ~SimpleIoError() override = default;

  IoErrorCode getErrorCode() const override { return code_; }
  std::string getErrorDetails() const override { return "CompositeIoHandle error"; }
  int getSystemErrorCode() const override { return 0; }

private:
  IoErrorCode code_;
};

Api::IoErrorPtr errorFromCode(absl::optional<Api::IoError::IoErrorCode> err_code) {
  if (!err_code.has_value()) {
    return Api::IoError::none();
  }
  if (err_code.value() == Api::IoError::IoErrorCode::Again) {
    return Network::IoSocketError::getIoSocketEagainError();
  }
  return Api::IoErrorPtr(new SimpleIoError(err_code.value()), Api::IoError::wrap(nullptr).get_deleter());
}

// CompositeIoHandle wraps the IoHandle of the underlying connection (via outer_socket)
// but delegates read/write operations to the outer_socket's doRead/doWrite.
// This allows the inner_socket to "write" to the outer_socket as if it were a file descriptor.
class CompositeIoHandle : public Network::IoHandle {
public:
  CompositeIoHandle(Network::IoHandle& underlying_io_handle, Network::TransportSocket& outer_socket)
      : underlying_io_handle_(underlying_io_handle), outer_socket_(outer_socket) {}

  // Delegate all control/info methods to the underlying IoHandle
  os_fd_t fdDoNotUse() const override { return underlying_io_handle_.fdDoNotUse(); }
  Api::IoCallUint64Result close() override { return underlying_io_handle_.close(); }
  bool isOpen() const override { return underlying_io_handle_.isOpen(); }
  bool wasConnected() const override { return underlying_io_handle_.wasConnected(); }
  bool supportsMmsg() const override { return underlying_io_handle_.supportsMmsg(); }
  bool supportsUdpGro() const override { return underlying_io_handle_.supportsUdpGro(); }
  Api::SysCallIntResult bind(Network::Address::InstanceConstSharedPtr address) override {
    return underlying_io_handle_.bind(address);
  }
  Api::SysCallIntResult listen(int backlog) override { return underlying_io_handle_.listen(backlog); }
  std::unique_ptr<Network::IoHandle> accept(struct sockaddr* addr, socklen_t* addrlen) override {
    return underlying_io_handle_.accept(addr, addrlen);
  }
  Api::SysCallIntResult connect(Network::Address::InstanceConstSharedPtr address) override {
    return underlying_io_handle_.connect(address);
  }
  Api::SysCallIntResult setOption(int level, int optname, const void* optval,
                                  socklen_t optlen) override {
    return underlying_io_handle_.setOption(level, optname, optval, optlen);
  }
  Api::SysCallIntResult getOption(int level, int optname, void* optval,
                                  socklen_t* optlen) override {
    return underlying_io_handle_.getOption(level, optname, optval, optlen);
  }
  Api::SysCallIntResult ioctl(unsigned long control_code, void* in_buffer,
                              unsigned long in_buffer_len, void* out_buffer,
                              unsigned long out_buffer_len,
                              unsigned long* bytes_returned) override {
    return underlying_io_handle_.ioctl(control_code, in_buffer, in_buffer_len, out_buffer,
                                       out_buffer_len, bytes_returned);
  }
  Api::SysCallIntResult setBlocking(bool blocking) override {
    return underlying_io_handle_.setBlocking(blocking);
  }
  absl::optional<int> domain() override { return underlying_io_handle_.domain(); }
  absl::StatusOr<Network::Address::InstanceConstSharedPtr> localAddress() override {
    return underlying_io_handle_.localAddress();
  }
  absl::StatusOr<Network::Address::InstanceConstSharedPtr> peerAddress() override {
    return underlying_io_handle_.peerAddress();
  }
  std::unique_ptr<Network::IoHandle> duplicate() override { return underlying_io_handle_.duplicate(); }
  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override {
    underlying_io_handle_.initializeFileEvent(dispatcher, cb, trigger, events);
  }
  void activateFileEvents(uint32_t events) override {
    underlying_io_handle_.activateFileEvents(events);
  }
  void enableFileEvents(uint32_t events) override {
    underlying_io_handle_.enableFileEvents(events);
  }
  void resetFileEvents() override { underlying_io_handle_.resetFileEvents(); }
  Api::SysCallIntResult shutdown(int how) override { return underlying_io_handle_.shutdown(how); }
  absl::optional<std::chrono::milliseconds> lastRoundTripTime() override {
    return underlying_io_handle_.lastRoundTripTime();
  }
  absl::optional<uint64_t> congestionWindowInBytes() const override {
    return underlying_io_handle_.congestionWindowInBytes();
  }
  absl::optional<std::string> interfaceName() override { return underlying_io_handle_.interfaceName(); }

  // Delegate data transfer methods to the Outer Transport Socket
  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override {
    Buffer::OwnedImpl read_buffer;
    
    // Use doRead to read from outer socket
    Network::IoResult result = outer_socket_.doRead(read_buffer);
    
    // Check for error
    if (result.err_code_.has_value()) {
       return {0, errorFromCode(result.err_code_)};
    }
    
    uint64_t bytes_read = read_buffer.length();
    
    // doRead appends to buffer.
    uint64_t bytes_to_copy = std::min(bytes_read, max_length);
    
    // Copy to slices
    uint64_t copied = 0;
    for (uint64_t i = 0; i < num_slice && copied < bytes_to_copy; ++i) {
      uint64_t slice_len = slices[i].len_;
      uint64_t copy_len = std::min(slice_len, bytes_to_copy - copied);
      read_buffer.copyOut(copied, copy_len, slices[i].mem_);
      copied += copy_len;
    }
    
    // Save leftover data
    if (copied < bytes_read) {
         if (!read_buffer_) {
             read_buffer_ = std::make_unique<Buffer::OwnedImpl>();
         }
         read_buffer_->move(read_buffer, bytes_read - copied);
    }
    
    return {copied, Api::IoError::none()};
  }

  Api::IoCallUint64Result read(Buffer::Instance& buffer,
                               absl::optional<uint64_t> max_length) override {
      if (!read_buffer_) {
          read_buffer_ = std::make_unique<Buffer::OwnedImpl>();
      }

      // If we have no data buffered, try to read from the outer socket.
      if (read_buffer_->length() == 0) {
          Network::IoResult result = outer_socket_.doRead(*read_buffer_);
          if (result.err_code_.has_value()) {
              // If we got an error and no data was read, return the error.
              // Note: doRead might have added data even with error? 
              // Usually error implies no data or specific condition.
              // If data was read, we should probably return it? 
              // But IoResult has bytes_processed_.
              if (read_buffer_->length() == 0) {
                   return {0, errorFromCode(result.err_code_)};
              }
              // If we also got data, we might want to return it and ignore error for now?
              // Or return data and let next read pick up error?
              // Standard socket behavior: if we got data, return success (bytes read).
          }
      }

      uint64_t to_move = read_buffer_->length();
      if (max_length.has_value()) {
          to_move = std::min(to_move, max_length.value());
      }
      
      if (to_move > 0) {
          buffer.move(*read_buffer_, to_move);
      } else {
          // If we still have no data, it might indicate EOF or just no data available yet (if no error).
          // But if we called doRead and got 0 bytes and no error, it usually means EOF or nothing ready.
          // Api::IoCallUint64Result expects 0 and no error for success but 0 bytes.
      }
      
      return {to_move, Api::IoError::none()};
  }

  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override {
    Buffer::OwnedImpl buffer;
    for (uint64_t i = 0; i < num_slice; ++i) {
      buffer.add(slices[i].mem_, slices[i].len_);
    }
    Network::IoResult result = outer_socket_.doWrite(buffer, false); // Not end stream?
    if (result.err_code_.has_value()) {
        return {0, errorFromCode(result.err_code_)};
    }
    return {result.bytes_processed_, Api::IoError::none()};
  }

  Api::IoCallUint64Result write(Buffer::Instance& buffer) override {
    Network::IoResult result = outer_socket_.doWrite(buffer, false);
    if (result.err_code_.has_value()) {
        return {0, errorFromCode(result.err_code_)};
    }
    return {result.bytes_processed_, Api::IoError::none()};
  }

  Api::IoCallUint64Result sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice,
                                  int flags, const Network::Address::Ip* self_ip,
                                  const Network::Address::Instance& peer_address) override {
      return underlying_io_handle_.sendmsg(slices, num_slice, flags, self_ip, peer_address);
  }

  Api::IoCallUint64Result recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice,
                                  uint32_t self_port,
                                  const UdpSaveCmsgConfig& save_cmsg_config,
                                  RecvMsgOutput& output) override {
       return underlying_io_handle_.recvmsg(slices, num_slice, self_port, save_cmsg_config, output);
  }

  Api::IoCallUint64Result recvmmsg(RawSliceArrays& slices, uint32_t self_port,
                                   const UdpSaveCmsgConfig& save_cmsg_config,
                                   RecvMsgOutput& output) override {
       return underlying_io_handle_.recvmmsg(slices, self_port, save_cmsg_config, output);
  }

  Api::IoCallUint64Result recv(void* buffer, size_t length, int flags) override {
       // Not implemented for stream adaptation yet, usually used for UDP or raw sockets?
        return underlying_io_handle_.recv(buffer, length, flags);
  }

private:
  Network::IoHandle& underlying_io_handle_;
  Network::TransportSocket& outer_socket_;
  std::unique_ptr<Buffer::OwnedImpl> read_buffer_;
};

// FakeTransportSocketCallbacks wraps the real callbacks but returns our CompositeIoHandle.
class FakeTransportSocketCallbacks : public Network::TransportSocketCallbacks {
public:
  FakeTransportSocketCallbacks(Network::TransportSocketCallbacks& parent_callbacks,
                               Network::TransportSocket& outer_socket)
      : parent_callbacks_(parent_callbacks),
        // We act as if outer_socket is the IoHandle provider
        io_handle_(parent_callbacks.ioHandle(), outer_socket) {}

  Network::IoHandle& ioHandle() override { return io_handle_; }
  const Network::IoHandle& ioHandle() const override { return io_handle_; }
  Network::Connection& connection() override { return parent_callbacks_.connection(); }
  bool shouldDrainReadBuffer() override { return parent_callbacks_.shouldDrainReadBuffer(); }
  void setTransportSocketIsReadable() override { parent_callbacks_.setTransportSocketIsReadable(); }
  void raiseEvent(Network::ConnectionEvent event) override {
    parent_callbacks_.raiseEvent(event);
  }
  void flushWriteBuffer() override { parent_callbacks_.flushWriteBuffer(); }

private:
  Network::TransportSocketCallbacks& parent_callbacks_;
  CompositeIoHandle io_handle_;
};

} // namespace

CompositeTransportSocket::CompositeTransportSocket(Network::TransportSocketPtr outer_socket,
                                                   Network::TransportSocketPtr inner_socket)
    : outer_socket_(std::move(outer_socket)), inner_socket_(std::move(inner_socket)) {}

void CompositeTransportSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  outer_socket_->setTransportSocketCallbacks(callbacks);
  
  // Create wrappers for inner socket
  inner_callbacks_ = std::make_unique<FakeTransportSocketCallbacks>(callbacks, *outer_socket_);
  inner_socket_->setTransportSocketCallbacks(*inner_callbacks_);
}

std::string CompositeTransportSocket::protocol() const {
  // Return inner protocol? or composite?
  // Usually we want the application protocol, which is inner.
  return inner_socket_->protocol();
}

absl::string_view CompositeTransportSocket::failureReason() const {
  if (!outer_socket_->failureReason().empty()) {
    return outer_socket_->failureReason();
  }
  return inner_socket_->failureReason();
}

bool CompositeTransportSocket::canFlushClose() {
  return inner_socket_->canFlushClose();
}

void CompositeTransportSocket::closeSocket(Network::ConnectionEvent event) {
  inner_socket_->closeSocket(event);
  outer_socket_->closeSocket(event);
}

Network::IoResult CompositeTransportSocket::doRead(Buffer::Instance& buffer) {
  // We read from INNER socket. Inner socket will pull from Outer via CompositeIoHandle.
  return inner_socket_->doRead(buffer);
}

Network::IoResult CompositeTransportSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  // We write to INNER socket. Inner socket will push to Outer via CompositeIoHandle.
  return inner_socket_->doWrite(buffer, end_stream);
}

void CompositeTransportSocket::onConnected() {
  outer_socket_->onConnected();
  inner_socket_->onConnected();
}

Ssl::ConnectionInfoConstSharedPtr CompositeTransportSocket::ssl() const {
  // Usually we want the inner SSL info if it exists (e.g. if we are doing mTLS inside tunnel).
  // But if inner is raw_buffer, maybe we want outer?
  // Let's prefer inner, then outer.
  auto inner_ssl = inner_socket_->ssl();
  if (inner_ssl) {
    return inner_ssl;
  }
  return outer_socket_->ssl();
}

bool CompositeTransportSocket::startSecureTransport() {
  // Start outer first?
  bool outer_ok = outer_socket_->startSecureTransport();
  // If outer fails/pending?
  // Actually, we should probably start inner only after outer is ready?
  // But `onConnected` might be the trigger.
  // `startSecureTransport` is called by connection impl.
  bool inner_ok = inner_socket_->startSecureTransport();
  return outer_ok && inner_ok;
}

void CompositeTransportSocket::configureInitialCongestionWindow(uint64_t bandwidth_bits_per_sec,
                                                                std::chrono::microseconds rtt) {
  outer_socket_->configureInitialCongestionWindow(bandwidth_bits_per_sec, rtt);
  inner_socket_->configureInitialCongestionWindow(bandwidth_bits_per_sec, rtt);
}

} // namespace Composite
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
