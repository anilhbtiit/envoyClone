#include <process.h>

#include "common/common/assert.h"
#include "common/common/thread_impl.h"

namespace Envoy {
namespace Thread {

ThreadImpl::ThreadImpl(std::function<void()> thread_routine) : thread_routine_(thread_routine) {
  RELEASE_ASSERT(Logger::Registry::initialized(), "");
  thread_handle_ =
      reinterpret_cast<HANDLE>(::_beginthreadex(nullptr, 0,
                                                [](void* arg) -> unsigned int {
                                                  static_cast<ThreadImpl*>(arg)->thread_routine_();
                                                  return 0;
                                                },
                                                this, 0, nullptr));
  RELEASE_ASSERT(thread_handle_ != 0, "");
}

ThreadImpl::~ThreadImpl() { ::CloseHandle(thread_handle_); }

void ThreadImpl::join() {
  const DWORD rc = ::WaitForSingleObject(thread_handle_, INFINITE);
  RELEASE_ASSERT(rc == WAIT_OBJECT_0, "");
}

ThreadId currentThreadId() { return ::GetCurrentThreadId(); }

} // namespace Thread
} // namespace Envoy
