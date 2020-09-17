#pragma once

#include <cstdint>

#include "envoy/event/file_event.h"

#include "common/event/dispatcher_impl.h"
#include "common/event/event_impl_base.h"

namespace Envoy {
namespace Event {

/**
 * Implementation of FileEvent for libevent that uses persistent events and
 * assumes the user will read/write until EAGAIN is returned from the file.
 */
class FileEventImpl : public FileEvent, ImplBase {
public:
  FileEventImpl(DispatcherImpl& dispatcher, os_fd_t fd, FileReadyCb cb, FileTriggerType trigger,
                uint32_t events);

  // Event::FileEvent
  void activate(uint32_t events) override;
  void setEnabled(uint32_t events) override;

private:
  void assignEvents(uint32_t events, event_base* base);
  void mergeInjectedEventsAndRunCb(uint32_t events);

  FileReadyCb cb_;
  os_fd_t fd_;
  FileTriggerType trigger_;

  // Injected FileReadyType events that were scheduled by recent calls to activate() and are pending
  // delivery.
  uint32_t injected_activation_events_{};
  // Used to schedule delayed event activation. Armed iff pending_activation_events_ != 0.
  SchedulableCallbackPtr activation_cb_;
  // Latched "envoy.reloadable_features.activate_fds_next_event_loop" runtime feature. If true, fd
  // events scheduled via activate are evaluated in the next iteration of the event loop after
  // polling and activating new fd events.
  const bool activate_fd_events_next_event_loop_;
};

class TimerWrappedFileEventImpl : public FileEvent {
public:
  TimerWrappedFileEventImpl(SchedulableCallbackPtr schedulable)
      : schedulable_(std::move(schedulable)) {}

  ~TimerWrappedFileEventImpl() {
    if (schedulable_->enabled()) {
      schedulable_->cancel();
    }
  }
  // Event::FileEvent
  void activate(uint32_t) override { schedulable_->scheduleCallbackNextIteration(); }
  void setEnabled(uint32_t) override { schedulable_->scheduleCallbackNextIteration(); }

private:
  SchedulableCallbackPtr schedulable_;
};

/**
 * This file event is a helper event to be always active. It works with BufferedIoSocketHandleImpl
 * so that the socket handle will call io methods ASAP and obtain the error code.
 */
class AlwaysActiveFileEventImpl : public FileEvent {
public:
  AlwaysActiveFileEventImpl(SchedulableCallbackPtr schedulable)
      : schedulable_(std::move(schedulable)) {}

  ~AlwaysActiveFileEventImpl() override {
    if (schedulable_->enabled()) {
      schedulable_->cancel();
    }
  }
  // Event::FileEvent
  void activate(uint32_t) override { schedulable_->scheduleCallbackNextIteration(); }
  void setEnabled(uint32_t) override { schedulable_->scheduleCallbackNextIteration(); }

private:
  SchedulableCallbackPtr schedulable_;
};

} // namespace Event
} // namespace Envoy
