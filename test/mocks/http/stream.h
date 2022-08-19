#pragma once

#include "envoy/http/codec.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockStream : public Stream {
public:
  MockStream();
  ~MockStream() override;

  // Http::Stream
  MOCK_METHOD(void, addCallbacks, (StreamCallbacks & callbacks));
  MOCK_METHOD(void, removeCallbacks, (StreamCallbacks & callbacks));
  MOCK_METHOD(void, resetStream, (StreamResetReason reason));
  MOCK_METHOD(void, readDisable, (bool disable));
  MOCK_METHOD(void, setWriteBufferWatermarks, (uint32_t));
  MOCK_METHOD(uint32_t, bufferLimit, (), (const));
  MOCK_METHOD(const Network::Address::InstanceConstSharedPtr&, connectionLocalAddress, ());
  MOCK_METHOD(void, setFlushTimeout, (std::chrono::milliseconds timeout));
  MOCK_METHOD(void, setAccount, (Buffer::BufferMemoryAccountSharedPtr));

  // Use the same underlying structure as StreamCallbackHelper to insure iteration stability
  // if we remove callbacks during iteration.
  absl::InlinedVector<StreamCallbacks*, 8> callbacks_;
  Network::Address::InstanceConstSharedPtr connection_local_address_;
  Buffer::BufferMemoryAccountSharedPtr account_;

  void runHighWatermarkCallbacks() {
    for (auto* callback : callbacks_) {
      if (callback) {
        callback->onAboveWriteBufferHighWatermark();
      }
    }
  }

  void runLowWatermarkCallbacks() {
    for (auto* callback : callbacks_) {
      if (callback) {
        callback->onBelowWriteBufferLowWatermark();
      }
    }
  }

  void runStreamEndCallbacks() {
    for (auto* callback : callbacks_) {
      if (callback) {
        callback->onStreamEnd();
      }
    }
  }

  const StreamInfo::BytesMeterSharedPtr& bytesMeter() override { return bytes_meter_; }

  StreamInfo::BytesMeterSharedPtr bytes_meter_{std::make_shared<StreamInfo::BytesMeter>()};
};

} // namespace Http
} // namespace Envoy
