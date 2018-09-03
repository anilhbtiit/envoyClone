#include "server/config_validation/api.h"

#include "server/config_validation/dispatcher.h"

namespace Envoy {
namespace Api {

ValidationImpl::ValidationImpl(std::chrono::milliseconds file_flush_interval_msec)
    : Impl(file_flush_interval_msec) {}

Event::DispatcherPtr ValidationImpl::allocateDispatcher(TimeSource& time_source) {
  return Event::DispatcherPtr{new Event::ValidationDispatcher(time_source)};
}

} // namespace Api
} // namespace Envoy
