#pragma once

#include <pthread.h>

#include <functional>

#include "envoy/common/platform.h"
#include "envoy/thread/thread.h"

#include "absl/functional/any_invocable.h"

namespace Envoy {
namespace Thread {

class ThreadHandle {
public:
  explicit ThreadHandle(absl::AnyInvocable<void()> thread_routine);

  /** Returns the thread routine. */
  absl::AnyInvocable<void()>& routine();

  /** Returns the thread handle. */
  pthread_t& handle();

private:
  absl::AnyInvocable<void()> thread_routine_;
  pthread_t thread_handle_;
};

class PosixThread : public Thread {
public:
  PosixThread(ThreadHandle* thread_handle, OptionsOptConstRef options);
  ~PosixThread() override;

  // Envoy::Thread
  std::string name() const override;
  void join() override;

  /**
   * Returns true if the thread object identifies an active thread of execution,
   * false otherwise.
   * A thread that has finished executing code, but has not yet been joined is
   * still considered an active thread of execution and is therefore joinable.
   */
  bool joinable() const;

  /**
   * Returns the pthread ID. The thread ID returned from this call is the same
   * thread ID returned from `pthread_self()`:
   * https://man7.org/linux/man-pages/man3/pthread_self.3.html
   */
  ThreadId pthreadId() const;

private:
#if SUPPORTS_PTHREAD_NAMING
  // Attempts to get the name from the operating system, returning true and
  // updating 'name' if successful. Note that during normal operation this
  // may fail, if the thread exits prior to the system call.
  bool getNameFromOS(std::string& name);
#endif

  absl::AnyInvocable<void()> thread_routine_;
  ThreadHandle* thread_handle_;
  std::string name_;
  bool joined_{false};
};

using PosixThreadPtr = std::unique_ptr<PosixThread>;

class PosixThreadFactory;
using PosixThreadFactoryPtr = std::unique_ptr<PosixThreadFactory>;

/** An interface for POSIX `ThreadFactory` */
class PosixThreadFactory : public ThreadFactory {
public:
  //  /** Creates a new instance of `PosixThreadPtr`. */
  static PosixThreadFactoryPtr create();

  /**
   * Creates a new generic thread from the specified `thread_routine`. When the
   * thread cannot be created, this function will crash. When using this class
   * directly, prefer to use the overloaded `PosixThreadFactory::createThread`.
   */
  ThreadPtr createThread(std::function<void()> thread_routine, OptionsOptConstRef options) PURE;

  /**
   * Creates a new POSIX thread from the specified `thread_routine`. When
   * `crash_on_failure` is set to true, this function will crash when the thread
   * cannot be created; otherwise a `nullptr` will be returned.
   */
  virtual PosixThreadPtr createThread(absl::AnyInvocable<void()> thread_routine,
                                      OptionsOptConstRef options, bool crash_on_failure) PURE;

  /**
   * On Linux, `currentThreadId()` uses `gettid()` and it returns the kernel
   * thread ID. The thread ID returned from this call is not the same as the
   * thread ID returned from `currentPThreadId()`.
   */
  ThreadId currentThreadId() PURE;

  /** Returns the current pthread ID. It uses `pthread_self()`. */
  virtual ThreadId currentPthreadId() PURE;
};

} // namespace Thread
} // namespace Envoy
