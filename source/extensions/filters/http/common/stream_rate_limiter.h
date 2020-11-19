#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/runtime/runtime.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/token_bucket_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

/**
 * An HTTP stream rate limiter. Used in the fault filter and bandwidth filter.
 */
class StreamRateLimiter : Logger::Loggable<Logger::Id::filter> {
public:
  // We currently divide each second into 64 segments for the token bucket. Thus, the rate limit is
  // KiB per second, divided into 16 segments, ~16ms apart. 64 is used because it divides into 1024
  // evenly.
  static constexpr uint64_t DefaultFillRate = 64;

  /**
   * @param max_kbps maximum rate in KiB/s.
   * @param max_buffered_data maximum data to buffer before invoking the pause callback.
   * @param pause_data_cb callback invoked when the limiter has buffered too much data.
   * @param resume_data_cb callback invoked when the limiter has gone under the buffer limit.
   * @param write_data_cb callback invoked to write data to the stream.
   * @param continue_cb callback invoked to continue the stream. This is only used to continue
   *                    trailers that have been paused during body flush.
   * @param time_source the time source to run the token bucket with.
   * @param dispatcher the stream's dispatcher to use for creating timers.
   * @param scope the stream's scope
   */
  StreamRateLimiter(uint64_t max_kbps, uint64_t max_buffered_data,                    
                    std::function<void()> pause_data_cb, std::function<void()> resume_data_cb,
                    std::function<void(Buffer::Instance&, bool)> write_data_cb,
                    std::function<void()> continue_cb, TimeSource& time_source,
                    Event::Dispatcher& dispatcher, const ScopeTrackedObject& scope,
                    std::shared_ptr<TokenBucketImpl> token_bucket = nullptr,
                    uint64_t fill_rate = DefaultFillRate);

  /**
   * Called by the stream to write data. All data writes happen asynchronously, the stream should
   * be stopped after this call (all data will be drained from incoming_buffer).
   */
  void writeData(Buffer::Instance& incoming_buffer, bool end_stream);

  /**
   * Called if the stream receives trailers.
   * Returns true if the read buffer is not completely drained yet.
   */
  bool onTrailers();

  /**
   * Like the owning filter, we must handle inline destruction, so we have a destroy() method which
   * kills any callbacks.
   */
  void destroy() { token_timer_.reset(); }
  bool destroyed() { return token_timer_ == nullptr; }

private:
  void onTokenTimer();

  const uint64_t bytes_per_time_slice_;
  const uint64_t second_divisor_;
  const std::function<void(Buffer::Instance&, bool)> write_data_cb_;
  const std::function<void()> continue_cb_;
  const ScopeTrackedObject& scope_;
  Event::TimerPtr token_timer_;
  bool saw_data_{};
  bool saw_end_stream_{};
  bool saw_trailers_{};
  Buffer::WatermarkBuffer buffer_;
};
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
