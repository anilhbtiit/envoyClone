#include "test/test_common/test_time_system.h"

#include "envoy/event/timer.h"

#include "common/common/thread.h"

namespace Envoy {
namespace Event {

TestTimeSystem* SingletonTimeSystemHelper::timeSystem(const MakeTimeSystemFn& make_time_system) {
  Thread::LockGuard lock(mutex_);
  if (time_system_.get() == nullptr) {
    time_system_ = make_time_system();
  }
  return time_system_.get();
}

} // namespace Event
} // namespace Envoy
