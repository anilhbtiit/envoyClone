#pragma once

#include <chrono>
#include <string>

#include "envoy/api/api.h"
#include "envoy/event/timer.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/thread/thread.h"

namespace Envoy {
namespace Api {

/**
 * Implementation of Api::Api
 */
class Impl : public Api::Api {
public:
  // Convenience no-arg constructor for integration and unit tests
  Impl();
  Impl(std::chrono::milliseconds file_flush_interval_msec, Thread::ThreadSystem& thread_system);

  // Api::Api
  Event::DispatcherPtr allocateDispatcher(Event::TimeSystem& time_system) override;
  Filesystem::FileSharedPtr createFile(const std::string& path, Event::Dispatcher& dispatcher,
                                       Thread::BasicLockable& lock,
                                       Stats::Store& stats_store) override;
  bool fileExists(const std::string& path) override;
  std::string fileReadToEnd(const std::string& path) override;
  Thread::ThreadPtr createThread(std::function<void()> thread_routine) override;

private:
  std::chrono::milliseconds file_flush_interval_msec_;
  Thread::ThreadSystem& thread_system_;
};

} // namespace Api
} // namespace Envoy
