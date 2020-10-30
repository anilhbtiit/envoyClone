#include "common/event/user_space_file_event_impl.h"

#include <cstdint>

#include "common/common/assert.h"
#include "common/network/peer_buffer.h"

namespace Envoy {
namespace Event {

UserSpaceFileEventImpl::UserSpaceFileEventImpl(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                                               uint32_t events, Network::ReadWritable& io_source)
    : schedulable_(dispatcher.createSchedulableCallback([this]() { cb_(); })), cb_([this, cb]() {
        auto ephemeral_events = event_listener_.getAndClearEphemeralEvents();
        ENVOY_LOG(trace, "User space event {} invokes callbacks on events = {}",
                  static_cast<void*>(this), ephemeral_events);
        cb(ephemeral_events);
      }),
      io_source_(io_source) {
  setEnabled(events);
}

void EventListenerImpl::onEventEnabled(uint32_t) {
  // Clear ephemeral events to align with FileEventImpl::setEnable().
  ephemeral_events_ = 0;
}

void EventListenerImpl::onEventActivated(uint32_t activated_events) {
  // Normally event owner should not activate any event which is disabled. Known exceptions includes
  // ConsumerWantsToRead() == true.
  // TODO(lambdai): Stricter check.
  ephemeral_events_ |= activated_events;
}

void UserSpaceFileEventImpl::activate(uint32_t events) {
  // Only supported event types are set.
  ASSERT((events & (FileReadyType::Read | FileReadyType::Write | FileReadyType::Closed)) == events);
  event_listener_.onEventActivated(events);
  schedulable_->scheduleCallbackNextIteration();
}

void UserSpaceFileEventImpl::setEnabled(uint32_t events) {
  // Only supported event types are set.
  ASSERT((events & (FileReadyType::Read | FileReadyType::Write | FileReadyType::Closed)) == events);
  event_listener_.onEventEnabled(events);
  bool was_enabled = schedulable_->enabled();
  // Recalculate activated events.
  uint32_t events_to_notify = 0;
  if ((events & FileReadyType::Read) && io_source_.isReadable()) {
    events_to_notify |= FileReadyType::Read;
  }
  if ((events & FileReadyType::Write) && io_source_.isPeerWritable()) {
    events_to_notify |= FileReadyType::Write;
  }
  if (events_to_notify != 0) {
    activate(events_to_notify);
  } else {
    schedulable_->cancel();
  }
  ENVOY_LOG(trace, "User space file event {} set events {}. Will {} reschedule.",
            static_cast<void*>(this), events, was_enabled ? "not " : "");
}

} // namespace Event
} // namespace Envoy