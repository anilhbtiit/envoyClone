#pragma once

#include <cstdint>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/utility.h"

#include "date_provider.h"

namespace Http {

/**
 * Base for all providers.
 */
class DateProviderImplBase : public DateProvider {
protected:
  static DateFormatter date_formatter_;
};

/**
 * A caching thread local provider. This implementation updates the date string every 500ms and
 * caches on each thread.
 */
class TlsCachingDateProviderImpl : public DateProviderImplBase {
public:
  TlsCachingDateProviderImpl(Event::Dispatcher& dispatcher, ThreadLocal::Instance& tls);

  // Http::DateProvider
  void setDateHeader(HeaderMap& headers) override;

private:
  struct ThreadLocalCachedDate : public ThreadLocal::ThreadLocalObject {
    ThreadLocalCachedDate(const std::string& date_string) : date_string_(date_string) {}

    // ThreadLocal::ThreadLocalObject
    void shutdown() override {}

    const std::string date_string_;
  };

  void onRefreshDate();

  ThreadLocal::Instance& tls_;
  uint32_t tls_slot_;
  Event::TimerPtr refresh_timer_;
};

/**
 * A basic provider that just creates the date string every time.
 */
class SlowDateProviderImpl : public DateProviderImplBase {
public:
  // Http::DateProvider
  void setDateHeader(HeaderMap& headers) override;
};

} // Http
