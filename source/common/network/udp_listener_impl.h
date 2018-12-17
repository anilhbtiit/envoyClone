#pragma once

#include "common/event/event_impl_base.h"

#include "base_listener_impl.h"

namespace Envoy {
namespace Network {

/**
 * libevent implementation of Network::Listener for UDP.
 */
class UdpListenerImpl : public BaseListenerImpl, public Event::ImplBase {
public:
  UdpListenerImpl(const Event::DispatcherImpl& dispatcher, Socket& socket, UdpListenerCallbacks& cb,
                  bool bind_to_port);

  virtual void disable() override;
  virtual void enable() override;

protected:
  UdpListenerCallbacks& cb_;

private:
  static void readCallback(int fd, short flags, void* arg);
};

} // namespace Network
} // namespace Envoy
