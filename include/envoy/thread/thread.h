#pragma once

#include <functional>
#include <memory>

#include "envoy/common/pure.h"

#include "common/common/thread_annotations.h"

namespace Envoy {
namespace Thread {

class ThreadId {
public:
  virtual ~ThreadId() {}

  virtual std::string debugString() const PURE;
  virtual bool isCurrentThreadId() const PURE;
};

typedef std::unique_ptr<ThreadId> ThreadIdPtr;

class Thread {
public:
  virtual ~Thread() {}

  /**
   * Join on thread exit.
   */
  virtual void join() PURE;
};

typedef std::unique_ptr<Thread> ThreadPtr;

/**
 * Interface providing a mechanism for creating threads.
 */
class ThreadFactory {
public:
  virtual ~ThreadFactory() {}

  /**
   * Create a thread.
   * @param thread_routine supplies the function to invoke in the thread.
   */
  virtual ThreadPtr createThread(std::function<void()> thread_routine) PURE;

  /**
   * Return the current system thread ID
   */
  virtual ThreadIdPtr currentThreadId() PURE;
};

#ifndef NDEBUG
/**
 * A debug only static singleton to the ThreadFactory corresponding to the build platform.
 *
 * The singleton must be initialized via set() early in main() with the appropriate ThreadFactory
 * (see source/exe/{posix,win32}/platform_impl.h).
 *
 * Debug only statements (such as ASSERT()) can then access the global ThreadFactory instance via
 * get().
 */
class ThreadFactorySingleton {
public:
  static ThreadFactory* get() { return thread_factory_; }

  static void set(ThreadFactory* thread_factory) { thread_factory_ = thread_factory; }

private:
  static ThreadFactory* thread_factory_;
};
#endif

/**
 * Like the C++11 "basic lockable concept" but a pure virtual interface vs. a template, and
 * with thread annotations.
 */
class LOCKABLE BasicLockable {
public:
  virtual ~BasicLockable() {}

  virtual void lock() EXCLUSIVE_LOCK_FUNCTION() PURE;
  virtual bool tryLock() EXCLUSIVE_TRYLOCK_FUNCTION(true) PURE;
  virtual void unlock() UNLOCK_FUNCTION() PURE;
};

} // namespace Thread
} // namespace Envoy
