#include "source/extensions/transport_sockets/composite/composite.h"
#include "source/extensions/transport_sockets/composite/config.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::Ref;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Composite {
namespace {

class CompositeTransportSocketTest : public testing::Test {
public:
  CompositeTransportSocketTest() {
    auto outer = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    auto inner = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    outer_socket_ = outer.get();
    inner_socket_ = inner.get();

    composite_socket_ = std::make_unique<CompositeTransportSocket>(std::move(outer), std::move(inner));
    
    ON_CALL(callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle_));
  }

  NiceMock<Network::MockTransportSocket>* outer_socket_;
  NiceMock<Network::MockTransportSocket>* inner_socket_;
  std::unique_ptr<CompositeTransportSocket> composite_socket_;
  NiceMock<Network::MockTransportSocketCallbacks> callbacks_;
  NiceMock<Network::MockIoHandle> io_handle_;
};

TEST_F(CompositeTransportSocketTest, SetCallbacksConfiguresBothSockets) {
  EXPECT_CALL(*outer_socket_, setTransportSocketCallbacks(_));
  EXPECT_CALL(*inner_socket_, setTransportSocketCallbacks(_))
      .WillOnce(testing::Invoke([&](Network::TransportSocketCallbacks& cb) {
         // Verify that the callbacks passed to inner socket return a CompositeIoHandle
         EXPECT_NE(&cb.ioHandle(), &callbacks_.ioHandle());
      }));

  composite_socket_->setTransportSocketCallbacks(callbacks_);
}

TEST_F(CompositeTransportSocketTest, DoReadDelegatesToInner) {
  Buffer::OwnedImpl buffer;
  Network::IoResult result{Network::PostIoAction::KeepOpen, 10, false};
  EXPECT_CALL(*inner_socket_, doRead(Ref(buffer))).WillOnce(Return(result));
  
  auto res = composite_socket_->doRead(buffer);
  EXPECT_EQ(res.bytes_processed_, 10);
}

TEST_F(CompositeTransportSocketTest, DoWriteDelegatesToInner) {
  Buffer::OwnedImpl buffer;
  Network::IoResult result{Network::PostIoAction::KeepOpen, 20, false};
  EXPECT_CALL(*inner_socket_, doWrite(Ref(buffer), true)).WillOnce(Return(result));

  auto res = composite_socket_->doWrite(buffer, true);
  EXPECT_EQ(res.bytes_processed_, 20);
}

TEST_F(CompositeTransportSocketTest, OnConnectedDelegatesToBoth) {
  testing::InSequence s;
  EXPECT_CALL(*outer_socket_, onConnected());
  EXPECT_CALL(*inner_socket_, onConnected());
  
  composite_socket_->onConnected();
}

TEST_F(CompositeTransportSocketTest, CloseSocketDelegatesToBoth) {
  // Order might matter or not, currently implementation does inner then outer.
  EXPECT_CALL(*inner_socket_, closeSocket(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(*outer_socket_, closeSocket(Network::ConnectionEvent::LocalClose));

  composite_socket_->closeSocket(Network::ConnectionEvent::LocalClose);
}


class CompositeNestedTest : public testing::Test {
public:
  CompositeNestedTest() {
    auto root_outer = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    auto middle_outer = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    auto core_inner = std::make_unique<NiceMock<Network::MockTransportSocket>>();

    root_outer_ = root_outer.get();
    middle_outer_ = middle_outer.get();
    core_inner_ = core_inner.get();

    auto composite2 = std::make_unique<CompositeTransportSocket>(std::move(middle_outer), std::move(core_inner));
    composite_socket_ = std::make_unique<CompositeTransportSocket>(std::move(root_outer), std::move(composite2));
    
    ON_CALL(callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle_));
  }

  NiceMock<Network::MockTransportSocket>* root_outer_;
  NiceMock<Network::MockTransportSocket>* middle_outer_;
  NiceMock<Network::MockTransportSocket>* core_inner_;
  std::unique_ptr<CompositeTransportSocket> composite_socket_;
  NiceMock<Network::MockTransportSocketCallbacks> callbacks_;
  NiceMock<Network::MockIoHandle> io_handle_;
};

TEST_F(CompositeNestedTest, TripleNestingCallFlow) {
  // Structure: Composite1(Outer=RootOuter, Inner=Composite2(Outer=MiddleOuter, Inner=CoreInner))
  // Data Flow (Write): CoreInner writes to IoHandle2 -> MiddleOuter.doWrite -> IoHandle1 -> RootOuter.doWrite -> RealIoHandle
  
  // 1. Setup Callbacks
  // We want to capture the IoHandles exposed to CoreInner and MiddleOuter to verify they are distinct and correct.
  Network::IoHandle* core_inner_io_handle = nullptr;
  
  EXPECT_CALL(*root_outer_, setTransportSocketCallbacks(_));
  EXPECT_CALL(*middle_outer_, setTransportSocketCallbacks(_));
  EXPECT_CALL(*core_inner_, setTransportSocketCallbacks(_))
      .WillOnce(testing::Invoke([&](Network::TransportSocketCallbacks& cb) {
          core_inner_io_handle = &cb.ioHandle();
      }));

  composite_socket_->setTransportSocketCallbacks(callbacks_);
  ASSERT_NE(core_inner_io_handle, nullptr);
  ASSERT_NE(core_inner_io_handle, &callbacks_.ioHandle());

  // 2. Verify Write Flow "Up" the stack (Inner -> Outer)
  // When CoreInner writes to its IoHandle, it should trigger MiddleOuter->doWrite
  Buffer::OwnedImpl data_from_core("data");
  
  // We verify that MiddleOuter receives the data
  EXPECT_CALL(*middle_outer_, doWrite(_, false))
      .WillOnce(testing::Invoke([&](Buffer::Instance& buf, bool) {
          EXPECT_EQ(buf.toString(), "data");
          // Simulate MiddleOuter writing to ITS IoHandle (which goes to RootOuter)
          // Since we don't have easy access to MiddleOuter's callbacks here without capturing them, 
          // we can verify the chain by inspection or by mocking the return/behavior.
          return Network::IoResult{Network::PostIoAction::KeepOpen, 4, false};
      }));

  // Perform the write on the innermost IoHandle
  Api::IoCallUint64Result write_res = core_inner_io_handle->write(data_from_core);
  EXPECT_TRUE(write_res.ok());
  EXPECT_EQ(write_res.return_value_, 4);

  // 3. Verify Read Flow "Down" (Outer -> Inner)
  // When CoreInner reads from its IoHandle, it should trigger MiddleOuter->doRead
  EXPECT_CALL(*middle_outer_, doRead(_))
      .WillOnce(testing::Invoke([&](Buffer::Instance& buf) {
          buf.add("decrypted_by_middle");
          return Network::IoResult{Network::PostIoAction::KeepOpen, 0, false};
      }));
  
  Buffer::OwnedImpl read_buf;
  Api::IoCallUint64Result read_res = core_inner_io_handle->read(read_buf, 100);
  EXPECT_TRUE(read_res.ok());
  EXPECT_EQ(read_buf.toString(), "decrypted_by_middle");
}

class CompositeIoHandleTest : public testing::Test {
public:
  CompositeIoHandleTest() {
    auto outer = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    auto inner = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    outer_socket_ = outer.get();
    inner_socket_ = inner.get();

    composite_socket_ = std::make_unique<CompositeTransportSocket>(std::move(outer), std::move(inner));
    ON_CALL(callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle_));
  }

  void SetUp() override {
    EXPECT_CALL(*outer_socket_, setTransportSocketCallbacks(_));
    EXPECT_CALL(*inner_socket_, setTransportSocketCallbacks(_))
        .WillOnce(testing::Invoke([&](Network::TransportSocketCallbacks& cb) {
           inner_io_handle_ = &cb.ioHandle();
        }));
    composite_socket_->setTransportSocketCallbacks(callbacks_);
  }

  NiceMock<Network::MockTransportSocket>* outer_socket_;
  NiceMock<Network::MockTransportSocket>* inner_socket_;
  std::unique_ptr<CompositeTransportSocket> composite_socket_;
  NiceMock<Network::MockTransportSocketCallbacks> callbacks_;
  NiceMock<Network::MockIoHandle> io_handle_;
  Network::IoHandle* inner_io_handle_;
};

TEST_F(CompositeIoHandleTest, ReadBufferingLogic) {
  // Test that if Outer socket returns MORE data than requested, CompositeIoHandle buffers it.
  
  // 1. Setup Outer to return "helloworld" (10 bytes)
  EXPECT_CALL(*outer_socket_, doRead(_))
      .WillOnce(testing::Invoke([&](Buffer::Instance& buf) {
          buf.add("helloworld");
          return Network::IoResult{Network::PostIoAction::KeepOpen, 10, false};
      }));

  // 2. Request only 5 bytes
  Buffer::OwnedImpl read_buf;
  Api::IoCallUint64Result res = inner_io_handle_->read(read_buf, 5);
  
  EXPECT_TRUE(res.ok());
  EXPECT_EQ(res.return_value_, 5);
  EXPECT_EQ(read_buf.toString(), "hello");
  
  // 3. Request next 5 bytes (should come from buffer, NOT trigger new doRead)
  EXPECT_CALL(*outer_socket_, doRead(_)).Times(0); // Should not be called
  
  res = inner_io_handle_->read(read_buf, 5);
  EXPECT_TRUE(res.ok());
  EXPECT_EQ(res.return_value_, 5);
  EXPECT_EQ(read_buf.toString(), "helloworld");
}

TEST_F(CompositeIoHandleTest, WriteDelegation) {
    Buffer::OwnedImpl write_data("test");
    EXPECT_CALL(*outer_socket_, doWrite(_, false))
        .WillOnce(testing::Invoke([&](Buffer::Instance& buf, bool) {
            EXPECT_EQ(buf.toString(), "test");
            return Network::IoResult{Network::PostIoAction::KeepOpen, 4, false};
        }));
    
    auto res = inner_io_handle_->write(write_data);
    EXPECT_TRUE(res.ok());
    EXPECT_EQ(res.return_value_, 4);
}

} // namespace
} // namespace Composite
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

