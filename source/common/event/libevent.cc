#include "common/event/libevent.h"

#include <signal.h>

#include "common/common/assert.h"

#include "event2/thread.h"

namespace Envoy {
namespace Event {
namespace Libevent {

static bool kInitialized = false;

bool Global::initialized() { return kInitialized; }

void Global::initialize() {
  evthread_use_pthreads();

  // Ignore SIGPIPE and allow errors to propagate through error codes.
  signal(SIGPIPE, SIG_IGN);
  kInitialized = true;
}

} // namespace Libevent
} // namespace Event
} // namespace Envoy
