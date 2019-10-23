#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/http/codec.h"

#include "common/http/codec_helper.h"

#include "extensions/quic_listeners/quiche/envoy_quic_simulated_watermark_buffer.h"
#include "extensions/quic_listeners/quiche/quic_filter_manager_connection_impl.h"

namespace Envoy {
namespace Quic {

// Base class for EnvoyQuicServer|ClientStream.
class EnvoyQuicStream : public Http::StreamEncoder,
                        public Http::Stream,
                        public Http::StreamCallbackHelper,
                        protected Logger::Loggable<Logger::Id::quic_stream> {
public:
  EnvoyQuicStream(uint32_t buffer_limit, std::function<void()> below_low_watermark,
                  std::function<void()> above_high_watermark)
      : send_buffer_simulation_(buffer_limit / 2, buffer_limit, std::move(below_low_watermark),
                                std::move(above_high_watermark), ENVOY_LOGGER()) {}

  // Http::StreamEncoder
  Stream& getStream() override { return *this; }

  // Http::Stream
  void readDisable(bool disable) override {
    bool status_changed{false};
    if (disable) {
      ++read_disable_counter_;
      if (read_disable_counter_ == 1) {
        status_changed = true;
      }
    } else {
      ASSERT(read_disable_counter_ > 0);
      --read_disable_counter_;
      if (read_disable_counter_ == 0) {
        status_changed = true;
      }
    }

    if (status_changed && !in_decode_data_callstack_) {
      // Avoid calling this while decoding data because transient disabling and
      // enabling reading may trigger another decoding data inside the
      // callstack which messes up stream state.
      switchStreamBlockState(disable);
    }
  }

  void addCallbacks(Http::StreamCallbacks& callbacks) override {
    ASSERT(!local_end_stream_);
    addCallbacks_(callbacks);
  }
  void removeCallbacks(Http::StreamCallbacks& callbacks) override { removeCallbacks_(callbacks); }
  uint32_t bufferLimit() override { return send_buffer_simulation_.highWatermark(); }

  // Needs to be called during quic stream creation before the stream receives
  // any headers and data.
  void setDecoder(Http::StreamDecoder& decoder) { decoder_ = &decoder; }

  void maybeCheckWatermark(uint64_t buffered_data_old, uint64_t buffered_data_new,
                           QuicFilterManagerConnectionImpl& connection) {
    if (buffered_data_new == buffered_data_old) {
      return;
    }
    // If buffered bytes changed, update stream and session's watermark book
    // keeping.
    if (buffered_data_new > buffered_data_old) {
      send_buffer_simulation_.checkHighWatermark(buffered_data_new);
    } else {
      send_buffer_simulation_.checkLowWatermark(buffered_data_new);
    }
    connection.adjustBytesToSend(buffered_data_new - buffered_data_old);
  }

protected:
  virtual void switchStreamBlockState(bool should_block) PURE;

  // Needed for ENVOY_STREAM_LOG.
  virtual uint32_t streamId() PURE;
  virtual Network::Connection* connection() PURE;

  Http::StreamDecoder* decoder() {
    ASSERT(decoder_ != nullptr);
    return decoder_;
  }

  // True once end of stream is propagated to Envoy. Envoy doesn't expect to be
  // notified more than once about end of stream. So once this is true, no need
  // to set it in the callback to Envoy stream any more.
  bool end_stream_decoded_{false};
  int32_t read_disable_counter_{0};
  // If true, switchStreamBlockState() should be deferred till this variable
  // becomes false.
  bool in_decode_data_callstack_{false};

private:
  // Not owned.
  Http::StreamDecoder* decoder_{nullptr};
  // Keeps track of bytes buffered in the stream send buffer in QUICHE and reacts
  // upon crossing high and low watermarks.
  // Its high watermark is also the buffer limit of stream read/write filters in
  // HCM.
  // There is no receive buffer simulation because Quic stream's
  // OnBodyDataAvailable() hands all the ready-to-use request data from stream sequencer to HCM
  // directly and buffers them in filters if needed. Itself doesn't buffer request data.
  EnvoyQuicSimulatedWatermarkBuffer send_buffer_simulation_;
};

} // namespace Quic
} // namespace Envoy
