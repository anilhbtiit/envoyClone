#include "common/common/fancy_logger.h"
#include "envoy/buffer/buffer.h"
#include "envoy/event/file_event.h"

#include "common/buffer/buffer_impl.h"
#include "common/network/address_impl.h"

#include "extensions/io_socket/buffered_io_socket/buffered_io_socket_handle_impl.h"

#include "test/mocks/event/mocks.h"

#include "absl/container/fixed_array.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace BufferedIoSocket {
namespace {

MATCHER(IsInvalidAddress, "") {
  return arg.err_->getErrorCode() == Api::IoError::IoErrorCode::NoSupport;
}

MATCHER(IsNotSupportedResult, "") { return arg.errno_ == SOCKET_ERROR_NOT_SUP; }

ABSL_MUST_USE_RESULT std::pair<Buffer::SlicePtr, Buffer::RawSlice> allocateOneSlice(uint64_t size) {
  auto owned_slice = Buffer::OwnedSlice::create(size);
  Buffer::RawSlice slice = owned_slice->reserve(size);
  EXPECT_NE(nullptr, slice.mem_);
  EXPECT_EQ(size, slice.len_);
  return {std::move(owned_slice), slice};
}

class MockFileEventCallback {
public:
  MOCK_METHOD(void, called, (uint32_t arg));
};

class BufferedIoSocketHandleTest : public testing::Test {
public:
  BufferedIoSocketHandleTest() : buf_(1024) {
    io_handle_ = std::make_unique<BufferedIoSocketHandleImpl>();
    io_handle_peer_ = std::make_unique<BufferedIoSocketHandleImpl>();
    io_handle_->setWritablePeer(io_handle_peer_.get());
    io_handle_peer_->setWritablePeer(io_handle_.get());
  }

  ~BufferedIoSocketHandleTest() override = default;

  Buffer::WatermarkBuffer& getWatermarkBufferHelper(BufferedIoSocketHandleImpl& io_handle) {
    return dynamic_cast<Buffer::WatermarkBuffer&>(*io_handle.getWriteBuffer());
  }

  NiceMock<Event::MockDispatcher> dispatcher_;

  // Owned by BufferedIoSocketHandle.
  NiceMock<Event::MockSchedulableCallback>* scheduable_cb_;
  MockFileEventCallback cb_;
  std::unique_ptr<BufferedIoSocketHandleImpl> io_handle_;
  std::unique_ptr<BufferedIoSocketHandleImpl> io_handle_peer_;
  absl::FixedArray<char> buf_;
};

// Test recv side effects.
TEST_F(BufferedIoSocketHandleTest, TestBasicRecv) {
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  internal_buffer.add("0123456789");
  {
    auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    ASSERT_EQ(10, result.rc_);
    ASSERT_EQ("0123456789", absl::string_view(buf_.data(), result.rc_));
  }
  {
    auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    // `EAGAIN`.
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());
  }
  {
    io_handle_->setWriteEnd();
    auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    EXPECT_TRUE(result.ok());
  }
}

// Test recv side effects.
TEST_F(BufferedIoSocketHandleTest, TestRecvPeek) {
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  internal_buffer.add("0123456789");
  {
    ::memset(buf_.data(), 1, buf_.size());
    auto result = io_handle_->recv(buf_.data(), 5, MSG_PEEK);
    ASSERT_EQ(5, result.rc_);
    ASSERT_EQ("01234", absl::string_view(buf_.data(), result.rc_));
    // The data beyond the boundary is untouched.
    ASSERT_EQ(std::string(buf_.size() - 5, 1), absl::string_view(buf_.data() + 5, buf_.size() - 5));
  }
  {
    auto result = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
    ASSERT_EQ(10, result.rc_);
    ASSERT_EQ("0123456789", absl::string_view(buf_.data(), result.rc_));
  }
  {
    // Drain the pending buffer.
    auto recv_result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    EXPECT_TRUE(recv_result.ok());
    EXPECT_EQ(10, recv_result.rc_);
    ASSERT_EQ("0123456789", absl::string_view(buf_.data(), recv_result.rc_));
    auto peek_result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    // `EAGAIN`.
    EXPECT_FALSE(peek_result.ok());
    EXPECT_EQ(Api::IoError::IoErrorCode::Again, peek_result.err_->getErrorCode());
  }
  {
    // Peek upon shutdown.
    io_handle_->setWriteEnd();
    auto result = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
    EXPECT_EQ(0, result.rc_);
    ASSERT(result.ok());
  }
}

TEST_F(BufferedIoSocketHandleTest, TestRecvPeekWhenPendingDataButShutdown) {
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  internal_buffer.add("0123456789");
  auto result = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
  ASSERT_EQ(10, result.rc_);
  ASSERT_EQ("0123456789", absl::string_view(buf_.data(), result.rc_));
}

TEST_F(BufferedIoSocketHandleTest, TestMultipleRecvDrain) {
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  internal_buffer.add("abcd");
  {
    auto result = io_handle_->recv(buf_.data(), 1, 0);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(1, result.rc_);
    EXPECT_EQ("a", absl::string_view(buf_.data(), 1));
  }
  {
    auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(3, result.rc_);

    EXPECT_EQ("bcd", absl::string_view(buf_.data(), 3));
    EXPECT_EQ(0, internal_buffer.length());
  }
}

// Test read side effects.
TEST_F(BufferedIoSocketHandleTest, TestReadEmpty) {
  Buffer::OwnedImpl buf;
  auto result = io_handle_->read(buf, 10);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());
  io_handle_->setWriteEnd();
  result = io_handle_->read(buf, 10);
  EXPECT_TRUE(result.ok());
}

// Test read side effects.
TEST_F(BufferedIoSocketHandleTest, TestReadContent) {
  Buffer::OwnedImpl buf;
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  internal_buffer.add("abcdefg");
  auto result = io_handle_->read(buf, 3);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(3, result.rc_);
  ASSERT_EQ(3, buf.length());
  ASSERT_EQ(4, internal_buffer.length());
  result = io_handle_->read(buf, 10);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(4, result.rc_);
  ASSERT_EQ(7, buf.length());
  ASSERT_EQ(0, internal_buffer.length());
}

// Test readv behavior.
TEST_F(BufferedIoSocketHandleTest, TestBasicReadv) {
  Buffer::OwnedImpl buf_to_write("abc");
  io_handle_peer_->write(buf_to_write);

  Buffer::OwnedImpl buf;
  Buffer::RawSlice slice;
  buf.reserve(1024, &slice, 1);
  auto result = io_handle_->readv(1024, &slice, 1);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(3, result.rc_);

  result = io_handle_->readv(1024, &slice, 1);

  EXPECT_FALSE(result.ok());
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());

  io_handle_->setWriteEnd();
  result = io_handle_->readv(1024, &slice, 1);
  // EOF
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(0, result.rc_);
}

TEST_F(BufferedIoSocketHandleTest, FlowControl) {
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  // TODO(lambdai): Make it an IoHandle method.
  internal_buffer.setWatermarks(128);
  EXPECT_FALSE(io_handle_->isReadable());
  EXPECT_TRUE(io_handle_->isWritable());

  // Populate the data for io_handle_.
  Buffer::OwnedImpl buffer(std::string(256, 'a'));
  io_handle_peer_->write(buffer);

  EXPECT_TRUE(io_handle_->isReadable());
  EXPECT_FALSE(io_handle_->isWritable());

  bool writable_flipped = false;
  // During the repeated recv, the writable flag must switch to true.
  while (internal_buffer.length() > 0) {
    SCOPED_TRACE(internal_buffer.length());
    FANCY_LOG(debug, "internal buffer length = {}", internal_buffer.length());
    EXPECT_TRUE(io_handle_->isReadable());
    bool writable = io_handle_->isWritable();
    FANCY_LOG(debug, "internal buffer length = {}, writable = {}", internal_buffer.length(),
              writable);
    if (writable) {
      writable_flipped = true;
    } else {
      ASSERT_FALSE(writable_flipped);
    }
    auto result = io_handle_->recv(buf_.data(), 32, 0);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(32, result.rc_);
  }
  ASSERT_EQ(0, internal_buffer.length());
  ASSERT_TRUE(writable_flipped);

  // Finally the buffer is empty.
  EXPECT_FALSE(io_handle_->isReadable());
  EXPECT_TRUE(io_handle_->isWritable());
}

// Consistent with other IoHandle: allow write empty data when handle is closed.
TEST_F(BufferedIoSocketHandleTest, TestNoErrorWriteZeroDataToClosedIoHandle) {
  io_handle_->close();
  {
    Buffer::OwnedImpl buf;
    auto result = io_handle_->write(buf);
    ASSERT_EQ(0, result.rc_);
    ASSERT(result.ok());
  }
  {
    Buffer::RawSlice slice{nullptr, 0};
    auto result = io_handle_->writev(&slice, 1);
    ASSERT_EQ(0, result.rc_);
    ASSERT(result.ok());
  }
}

TEST_F(BufferedIoSocketHandleTest, TestErrorOnClosedIoHandle) {
  io_handle_->close();
  {
    auto [guard, slice] = allocateOneSlice(1024);
    auto result = io_handle_->recv(slice.mem_, slice.len_, 0);
    ASSERT(!result.ok());
    ASSERT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());
  }
  {
    Buffer::OwnedImpl buf;
    auto result = io_handle_->read(buf, 10);
    ASSERT(!result.ok());
    ASSERT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());
  }
  {
    auto [guard, slice] = allocateOneSlice(1024);
    auto result = io_handle_->readv(1024, &slice, 1);
    ASSERT(!result.ok());
    ASSERT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());
  }
  {
    Buffer::OwnedImpl buf("0123456789");
    auto result = io_handle_->write(buf);
    ASSERT(!result.ok());
    ASSERT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());
  }
  {
    Buffer::OwnedImpl buf("0123456789");
    auto slices = buf.getRawSlices();
    ASSERT(!slices.empty());
    auto result = io_handle_->writev(slices.data(), slices.size());
    ASSERT(!result.ok());
    ASSERT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());
  }
}

TEST_F(BufferedIoSocketHandleTest, TestRepeatedShutdownWR) {
  EXPECT_EQ(io_handle_peer_->shutdown(ENVOY_SHUT_WR).rc_, 0);
  EXPECT_EQ(io_handle_peer_->shutdown(ENVOY_SHUT_WR).rc_, 0);
}

TEST_F(BufferedIoSocketHandleTest, TestShutDownOptionsNotSupported) {
  ASSERT_DEBUG_DEATH(io_handle_peer_->shutdown(ENVOY_SHUT_RD), "");
  ASSERT_DEBUG_DEATH(io_handle_peer_->shutdown(ENVOY_SHUT_RDWR), "");
}

TEST_F(BufferedIoSocketHandleTest, TestWriteByMove) {
  Buffer::OwnedImpl buf("0123456789");
  auto result = io_handle_peer_->write(buf);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(10, result.rc_);
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  EXPECT_EQ("0123456789", internal_buffer.toString());
  EXPECT_EQ(0, buf.length());
}

// Test write return error code. Ignoring the side effect of event scheduling.
TEST_F(BufferedIoSocketHandleTest, TestWriteAgain) {
  Buffer::OwnedImpl buf("0123456789");

  // Populate write destination with massive data so as to not writable.
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_peer_);
  internal_buffer.setWatermarks(1024);
  internal_buffer.add(std::string(2048, ' '));

  auto result = io_handle_->write(buf);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);
  EXPECT_EQ(10, buf.length());
}

TEST_F(BufferedIoSocketHandleTest, TestWriteErrorAfterShutdown) {
  Buffer::OwnedImpl buf("0123456789");
  // Write after shutdown.
  io_handle_->shutdown(ENVOY_SHUT_WR);
  auto result = io_handle_->write(buf);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::UnknownError);
  EXPECT_EQ(10, buf.length());
}

TEST_F(BufferedIoSocketHandleTest, TestWriteErrorAfterClose) {
  Buffer::OwnedImpl buf("0123456789");
  io_handle_peer_->close();
  EXPECT_TRUE(io_handle_->isOpen());
  auto result = io_handle_->write(buf);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::UnknownError);
}

// Test writev return error code. Ignoring the side effect of event scheduling.
TEST_F(BufferedIoSocketHandleTest, TestWritevAgain) {
  auto [guard, slice] = allocateOneSlice(128);
  // Populate write destination with massive data so as to not writable.
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_peer_);
  internal_buffer.setWatermarks(128);
  internal_buffer.add(std::string(256, ' '));
  auto result = io_handle_->writev(&slice, 1);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);
}

TEST_F(BufferedIoSocketHandleTest, TestWritevErrorAfterShutdown) {
  auto [guard, slice] = allocateOneSlice(128);
  // Writev after shutdown.
  io_handle_->shutdown(ENVOY_SHUT_WR);
  auto result = io_handle_->writev(&slice, 1);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::UnknownError);
}

TEST_F(BufferedIoSocketHandleTest, TestWritevErrorAfterClose) {
  auto [guard, slice] = allocateOneSlice(1024);
  // Close the peer.
  io_handle_peer_->close();
  EXPECT_TRUE(io_handle_->isOpen());
  auto result = io_handle_->writev(&slice, 1);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::UnknownError);
}

TEST_F(BufferedIoSocketHandleTest, TestWritevToPeer) {
  std::string raw_data("0123456789");
  absl::InlinedVector<Buffer::RawSlice, 4> slices{
      // Contains 1 byte.
      Buffer::RawSlice{static_cast<void*>(raw_data.data()), 1},
      // Contains 0 byte.
      Buffer::RawSlice{nullptr, 1},
      // Contains 0 byte.
      Buffer::RawSlice{raw_data.data() + 1, 0},
      // Contains 2 byte.
      Buffer::RawSlice{raw_data.data() + 1, 2},
  };
  io_handle_peer_->writev(slices.data(), slices.size());
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  EXPECT_EQ(3, internal_buffer.length());
  EXPECT_EQ("012", internal_buffer.toString());
}

TEST_F(BufferedIoSocketHandleTest, EventScheduleBasic) {
  auto scheduable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*scheduable_cb, enabled());
  EXPECT_CALL(*scheduable_cb, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Read | Event::FileReadyType::Write);

  EXPECT_CALL(cb_, called(Event::FileReadyType::Write));
  scheduable_cb->invokeCallback();
  io_handle_->resetFileEvents();
}

TEST_F(BufferedIoSocketHandleTest, TestSetEnabledTriggerEventSchedule) {
  auto scheduable_cb = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  // No data is available to read. Will not schedule read.
  {
    SCOPED_TRACE("enable read but no readable.");
    EXPECT_CALL(*scheduable_cb, enabled());
    EXPECT_CALL(*scheduable_cb, scheduleCallbackNextIteration()).Times(0);
    io_handle_->initializeFileEvent(
        dispatcher_, [this](uint32_t events) { cb_.called(events); }, Event::FileTriggerType::Edge,
        Event::FileReadyType::Read);
    testing::Mock::VerifyAndClearExpectations(scheduable_cb);
  }
  {
    SCOPED_TRACE("enable readwrite but only writable.");
    EXPECT_CALL(*scheduable_cb, enabled());
    EXPECT_CALL(*scheduable_cb, scheduleCallbackNextIteration());
    io_handle_->enableFileEvents(Event::FileReadyType::Read | Event::FileReadyType::Write);
    ASSERT_TRUE(scheduable_cb->enabled_);
    EXPECT_CALL(cb_, called(Event::FileReadyType::Write));
    scheduable_cb->invokeCallback();
    ASSERT_FALSE(scheduable_cb->enabled_);
    testing::Mock::VerifyAndClearExpectations(scheduable_cb);
  }
  {
    SCOPED_TRACE("enable write and writable.");
    EXPECT_CALL(*scheduable_cb, enabled());
    EXPECT_CALL(*scheduable_cb, scheduleCallbackNextIteration());
    io_handle_->enableFileEvents(Event::FileReadyType::Write);
    ASSERT_TRUE(scheduable_cb->enabled_);
    EXPECT_CALL(cb_, called(Event::FileReadyType::Write));
    scheduable_cb->invokeCallback();
    ASSERT_FALSE(scheduable_cb->enabled_);
    testing::Mock::VerifyAndClearExpectations(scheduable_cb);
  }
  // Close io_handle_ first to prevent events originated from peer close.
  io_handle_->close();
  io_handle_peer_->close();
}

TEST_F(BufferedIoSocketHandleTest, TestReadAndWriteAreEdgeTriggered) {
  auto scheduable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*scheduable_cb, enabled());
  EXPECT_CALL(*scheduable_cb, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Read | Event::FileReadyType::Write);

  EXPECT_CALL(cb_, called(Event::FileReadyType::Write));
  scheduable_cb->invokeCallback();

  Buffer::OwnedImpl buf("abcd");
  EXPECT_CALL(*scheduable_cb, scheduleCallbackNextIteration());
  io_handle_peer_->write(buf);

  EXPECT_CALL(cb_, called(Event::FileReadyType::Read));
  scheduable_cb->invokeCallback();

  // Drain 1 bytes.
  auto result = io_handle_->recv(buf_.data(), 1, 0);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(1, result.rc_);

  ASSERT_FALSE(scheduable_cb->enabled_);
  io_handle_->resetFileEvents();
}

TEST_F(BufferedIoSocketHandleTest, TestSetDisabledBlockEventSchedule) {
  auto scheduable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*scheduable_cb, enabled());
  EXPECT_CALL(*scheduable_cb, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Write);
  ASSERT_TRUE(scheduable_cb->enabled_);

  // The write event is cleared and the read event is not ready.
  EXPECT_CALL(*scheduable_cb, enabled());
  EXPECT_CALL(*scheduable_cb, cancel());
  io_handle_->enableFileEvents(Event::FileReadyType::Read);
  testing::Mock::VerifyAndClearExpectations(scheduable_cb);

  ASSERT_FALSE(scheduable_cb->enabled_);
  io_handle_->resetFileEvents();
}

TEST_F(BufferedIoSocketHandleTest, TestEventResetClearCallback) {
  auto scheduable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*scheduable_cb, enabled());
  EXPECT_CALL(*scheduable_cb, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Write);
  ASSERT_TRUE(scheduable_cb->enabled_);
  io_handle_->resetFileEvents();
}

TEST_F(BufferedIoSocketHandleTest, TestDrainToLowWaterMarkTriggerReadEvent) {
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  internal_buffer.setWatermarks(128);
  EXPECT_FALSE(io_handle_->isReadable());
  EXPECT_TRUE(io_handle_peer_->isWritable());

  std::string big_chunk(256, 'a');
  internal_buffer.add(big_chunk);
  EXPECT_TRUE(io_handle_->isReadable());
  EXPECT_FALSE(io_handle_->isWritable());

  auto scheduable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*scheduable_cb, enabled());
  // No event is available.
  EXPECT_CALL(*scheduable_cb, cancel());
  io_handle_peer_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Read | Event::FileReadyType::Write);
  // Neither readable nor writable.
  ASSERT_FALSE(scheduable_cb->enabled_);

  {
    SCOPED_TRACE("drain very few data.");
    auto result = io_handle_->recv(buf_.data(), 1, 0);
    EXPECT_FALSE(io_handle_->isWritable());
  }
  {
    SCOPED_TRACE("drain to low watermark.");
    EXPECT_CALL(*scheduable_cb, scheduleCallbackNextIteration()).Times(1);
    auto result = io_handle_->recv(buf_.data(), 232, 0);
    EXPECT_TRUE(io_handle_->isWritable());
    EXPECT_CALL(cb_, called(Event::FileReadyType::Write));
    scheduable_cb->invokeCallback();
  }
  {
    SCOPED_TRACE("clean up.");
    EXPECT_CALL(*scheduable_cb, scheduleCallbackNextIteration()).Times(1);
    // Important: close before peer.
    io_handle_->close();
  }
}

TEST_F(BufferedIoSocketHandleTest, TestClose) {
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  internal_buffer.add("abcd");
  std::string accumulator;
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  bool should_close = false;
  io_handle_->initializeFileEvent(
      dispatcher_,
      [this, &should_close, handle = io_handle_.get(), &accumulator](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          while (true) {
            auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
            if (result.ok()) {
              // Read EOF.
              if (result.rc_ == 0) {
                should_close = true;
                break;
              } else {
                accumulator += absl::string_view(buf_.data(), result.rc_);
              }
            } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
              ENVOY_LOG_MISC(debug, "read returns EAGAIN");
              break;
            } else {
              ENVOY_LOG_MISC(debug, "will close");
              should_close = true;
              break;
            }
          }
        }
        if (events & Event::FileReadyType::Write) {
          Buffer::OwnedImpl buf("");
          auto result = io_handle_->write(buf);
          if (!result.ok() && result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
            should_close = true;
          }
        }
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);
  scheduable_cb_->invokeCallback();

  // Not closed yet.
  ASSERT_FALSE(should_close);

  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_peer_->close();

  ASSERT_TRUE(scheduable_cb_->enabled());
  scheduable_cb_->invokeCallback();
  ASSERT_TRUE(should_close);

  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration()).Times(0);
  io_handle_->close();
  EXPECT_EQ(4, accumulator.size());
  io_handle_->resetFileEvents();
}

// Test that a readable event is raised when peer shutdown write. Also confirm read will return
// EAGAIN.
TEST_F(BufferedIoSocketHandleTest, TestShutDownRaiseEvent) {
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  internal_buffer.add("abcd");

  std::string accumulator;
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  bool should_close = false;
  io_handle_->initializeFileEvent(
      dispatcher_,
      [this, &should_close, handle = io_handle_.get(), &accumulator](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
          if (result.ok()) {
            accumulator += absl::string_view(buf_.data(), result.rc_);
          } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "read returns EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "will close");
            should_close = true;
          }
        }
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);
  scheduable_cb_->invokeCallback();

  // Not closed yet.
  ASSERT_FALSE(should_close);

  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_peer_->shutdown(ENVOY_SHUT_WR);

  ASSERT_TRUE(scheduable_cb_->enabled());
  scheduable_cb_->invokeCallback();
  ASSERT_FALSE(should_close);
  EXPECT_EQ(4, accumulator.size());
  io_handle_->close();
  io_handle_->resetFileEvents();
}

TEST_F(BufferedIoSocketHandleTest, TestWriteScheduleWritableEvent) {
  std::string accumulator;
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  bool should_close = false;
  io_handle_->initializeFileEvent(
      dispatcher_,
      [&should_close, handle = io_handle_.get(), &accumulator](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          Buffer::OwnedImpl buf;
          Buffer::RawSlice slice;
          buf.reserve(1024, &slice, 1);
          auto result = handle->readv(1024, &slice, 1);
          if (result.ok()) {
            accumulator += absl::string_view(static_cast<char*>(slice.mem_), result.rc_);
          } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "read returns EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "will close");
            should_close = true;
          }
        }
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);
  scheduable_cb_->invokeCallback();
  EXPECT_FALSE(scheduable_cb_->enabled());

  Buffer::OwnedImpl data_to_write("0123456789");
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_peer_->write(data_to_write);
  EXPECT_EQ(0, data_to_write.length());

  EXPECT_TRUE(scheduable_cb_->enabled());
  scheduable_cb_->invokeCallback();
  EXPECT_EQ("0123456789", accumulator);
  EXPECT_FALSE(should_close);

  io_handle_->close();
}

TEST_F(BufferedIoSocketHandleTest, TestWritevScheduleWritableEvent) {
  std::string accumulator;
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  bool should_close = false;
  io_handle_->initializeFileEvent(
      dispatcher_,
      [&should_close, handle = io_handle_.get(), &accumulator](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          Buffer::OwnedImpl buf;
          Buffer::RawSlice slice;
          buf.reserve(1024, &slice, 1);
          auto result = handle->readv(1024, &slice, 1);
          if (result.ok()) {
            accumulator += absl::string_view(static_cast<char*>(slice.mem_), result.rc_);
          } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "read returns EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "will close");
            should_close = true;
          }
        }
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);
  scheduable_cb_->invokeCallback();
  EXPECT_FALSE(scheduable_cb_->enabled());

  std::string raw_data("0123456789");
  Buffer::RawSlice slice{static_cast<void*>(raw_data.data()), raw_data.size()};
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_peer_->writev(&slice, 1);

  EXPECT_TRUE(scheduable_cb_->enabled());
  scheduable_cb_->invokeCallback();
  EXPECT_EQ("0123456789", accumulator);
  EXPECT_FALSE(should_close);

  io_handle_->close();
}

TEST_F(BufferedIoSocketHandleTest, TestReadAfterShutdownWrite) {
  io_handle_peer_->shutdown(ENVOY_SHUT_WR);
  ENVOY_LOG_MISC(debug, "after {} shutdown write ", static_cast<void*>(io_handle_peer_.get()));
  std::string accumulator;
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  bool should_close = false;
  io_handle_peer_->initializeFileEvent(
      dispatcher_,
      [&should_close, handle = io_handle_peer_.get(), &accumulator](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          Buffer::OwnedImpl buf;
          Buffer::RawSlice slice;
          buf.reserve(1024, &slice, 1);
          auto result = handle->readv(1024, &slice, 1);
          if (result.ok()) {
            if (result.rc_ == 0) {
              should_close = true;
            } else {
              accumulator += absl::string_view(static_cast<char*>(slice.mem_), result.rc_);
            }
          } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "read returns EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "will close");
            should_close = true;
          }
        }
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);

  EXPECT_FALSE(scheduable_cb_->enabled());
  std::string raw_data("0123456789");
  Buffer::RawSlice slice{static_cast<void*>(raw_data.data()), raw_data.size()};
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_->writev(&slice, 1);
  EXPECT_TRUE(scheduable_cb_->enabled());

  scheduable_cb_->invokeCallback();
  EXPECT_FALSE(scheduable_cb_->enabled());
  EXPECT_EQ(raw_data, accumulator);

  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_->close();
  io_handle_->resetFileEvents();
}

TEST_F(BufferedIoSocketHandleTest, TestNotififyWritableAfterShutdownWrite) {
  auto& peer_internal_buffer = getWatermarkBufferHelper(*io_handle_peer_);
  peer_internal_buffer.setWatermarks(128);
  // std::string big_chunk(256, 'a');
  //   peer_internal_buffer.add(big_chunk);
  Buffer::OwnedImpl buf(std::string(256, 'a'));
  io_handle_->write(buf);
  EXPECT_FALSE(io_handle_peer_->isWritable());

  io_handle_peer_->shutdown(ENVOY_SHUT_WR);
  FANCY_LOG(debug, "after {} shutdown write", static_cast<void*>(io_handle_peer_.get()));

  auto scheduable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*scheduable_cb, enabled());
  EXPECT_CALL(*scheduable_cb, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Read);
  EXPECT_CALL(cb_, called(Event::FileReadyType::Read));
  scheduable_cb->invokeCallback();
  EXPECT_FALSE(scheduable_cb->enabled_);

  EXPECT_CALL(*scheduable_cb, scheduleCallbackNextIteration());
  auto result = io_handle_peer_->recv(buf_.data(), buf_.size(), 0);
  EXPECT_EQ(256, result.rc_);
  EXPECT_TRUE(scheduable_cb->enabled_);

  io_handle_->close();
}

TEST_F(BufferedIoSocketHandleTest, TestNotSupportingMmsg) {
  EXPECT_FALSE(io_handle_->supportsMmsg());
}

TEST_F(BufferedIoSocketHandleTest, TestNotSupportsUdpGro) {
  EXPECT_FALSE(io_handle_->supportsUdpGro());
}

TEST_F(BufferedIoSocketHandleTest, TestDomainNullOpt) {
  EXPECT_FALSE(io_handle_->domain().has_value());
}

TEST_F(BufferedIoSocketHandleTest, TestConnect) {
  auto address_is_ignored =
      std::make_shared<Network::Address::EnvoyInternalInstance>("listener_id");
  EXPECT_EQ(0, io_handle_->connect(address_is_ignored).rc_);
}

TEST_F(BufferedIoSocketHandleTest, TestActivateEvent) {
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  io_handle_->initializeFileEvent(
      dispatcher_, [&, handle = io_handle_.get()](uint32_t) {}, Event::FileTriggerType::Edge,
      Event::FileReadyType::Read);
  EXPECT_FALSE(scheduable_cb_->enabled());
  io_handle_->activateFileEvents(Event::FileReadyType::Read);
  ASSERT_TRUE(scheduable_cb_->enabled());
}

TEST_F(BufferedIoSocketHandleTest, TestDeathOnActivatingDestroyedEvents) {
  io_handle_->resetFileEvents();
  ASSERT_DEBUG_DEATH(io_handle_->activateFileEvents(Event::FileReadyType::Read),
                     "Null user_file_event_");
}

TEST_F(BufferedIoSocketHandleTest, TestDeathOnEnablingDestroyedEvents) {
  io_handle_->resetFileEvents();
  ASSERT_DEBUG_DEATH(io_handle_->enableFileEvents(Event::FileReadyType::Read),
                     "Null user_file_event_");
}

TEST_F(BufferedIoSocketHandleTest, TestNotImplementDuplicate) {
  ASSERT_DEATH(io_handle_->duplicate(), "");
}

TEST_F(BufferedIoSocketHandleTest, TestNotImplementAccept) {
  ASSERT_DEATH(io_handle_->accept(nullptr, nullptr), "");
}

TEST_F(BufferedIoSocketHandleTest, TestLastRoundtripTimeNullOpt) {
  ASSERT_EQ(absl::nullopt, io_handle_->lastRoundTripTime());
}

class BufferedIoSocketHandleNotImplementedTest : public testing::Test {
public:
  BufferedIoSocketHandleNotImplementedTest() {
    io_handle_ = std::make_unique<BufferedIoSocketHandleImpl>();
    io_handle_peer_ = std::make_unique<BufferedIoSocketHandleImpl>();
    io_handle_->setWritablePeer(io_handle_peer_.get());
    io_handle_peer_->setWritablePeer(io_handle_.get());
  }

  ~BufferedIoSocketHandleNotImplementedTest() override {
    if (io_handle_->isOpen()) {
      io_handle_->close();
    }
    if (io_handle_peer_->isOpen()) {
      io_handle_peer_->close();
    }
  }

  std::unique_ptr<BufferedIoSocketHandleImpl> io_handle_;
  std::unique_ptr<BufferedIoSocketHandleImpl> io_handle_peer_;
  Buffer::RawSlice slice_;
};

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnSetBlocking) {
  EXPECT_THAT(io_handle_->setBlocking(false), IsNotSupportedResult());
  EXPECT_THAT(io_handle_->setBlocking(true), IsNotSupportedResult());
}

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnSendmsg) {
  EXPECT_THAT(io_handle_->sendmsg(&slice_, 0, 0, nullptr,
                                  Network::Address::EnvoyInternalInstance("listener_id")),
              IsInvalidAddress());
}

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnRecvmsg) {
  Network::IoHandle::RecvMsgOutput output_is_ignored(1, nullptr);
  EXPECT_THAT(io_handle_->recvmsg(&slice_, 0, 0, output_is_ignored), IsInvalidAddress());
}

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnRecvmmsg) {
  RawSliceArrays slices_is_ignored(1, absl::FixedArray<Buffer::RawSlice>({slice_}));
  Network::IoHandle::RecvMsgOutput output_is_ignored(1, nullptr);
  EXPECT_THAT(io_handle_->recvmmsg(slices_is_ignored, 0, output_is_ignored), IsInvalidAddress());
}

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnBind) {
  auto address_is_ignored =
      std::make_shared<Network::Address::EnvoyInternalInstance>("listener_id");
  EXPECT_THAT(io_handle_->bind(address_is_ignored), IsNotSupportedResult());
}

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnListen) {
  int back_log_is_ignored = 0;
  EXPECT_THAT(io_handle_->listen(back_log_is_ignored), IsNotSupportedResult());
}

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnAddress) {
  ASSERT_THROW(io_handle_->peerAddress(), EnvoyException);
  ASSERT_THROW(io_handle_->localAddress(), EnvoyException);
}

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnSetOption) {
  EXPECT_THAT(io_handle_->setOption(0, 0, nullptr, 0), IsNotSupportedResult());
}

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnGetOption) {
  EXPECT_THAT(io_handle_->getOption(0, 0, nullptr, nullptr), IsNotSupportedResult());
}
} // namespace
} // namespace BufferedIoSocket
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
