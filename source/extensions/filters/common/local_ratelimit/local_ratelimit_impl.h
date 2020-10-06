#pragma once

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "extensions/filters/common/local_ratelimit/local_ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

// TODO(htuch): We should have only one client per thread, but today we create one per filter stack.
// This will require support for more than one outstanding request per client (limit() assumes only
// one today).
class LocalRateLimiterImpl : public LocalRateLimiter {
public:
  LocalRateLimiterImpl(const std::chrono::milliseconds fill_interval, const uint32_t max_tokens,
                       const uint32_t tokens_per_fill, Event::Dispatcher& dispatcher);
  ~LocalRateLimiterImpl() override;

  // Filters::Common::LocalRateLimit::LocalRateLimiter
  bool requestAllowed() const override;

private:
  void onFillTimer();

  const std::chrono::milliseconds fill_interval_;
  const uint32_t max_tokens_;
  const uint32_t tokens_per_fill_;
  const Event::TimerPtr fill_timer_;
  mutable std::atomic<uint32_t> tokens_;
};

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
