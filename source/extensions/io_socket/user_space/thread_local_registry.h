#pragma once

#include "envoy/thread_local/thread_local_object.h"
#include "envoy/network/listener.h"

namespace Envoy {
namespace Extensions {
namespace InternalListener {

class ThreadLocalRegistryImpl : public ThreadLocal::ThreadLocalObject,
                                public Network::LocalInternalListenerRegistry {
public:
  ThreadLocalRegistryImpl() = default;

  void
  setInternalListenerManager(Network::InternalListenerManager& internal_listener_manager) override {

    ASSERT(manager_ == nullptr);
    manager_ = &internal_listener_manager;
  }

  Network::InternalListenerManagerOptRef getInternalListenerManager() override {
    if (manager_ == nullptr) {
      return Network::InternalListenerManagerOptRef();
    }
    return Network::InternalListenerManagerOptRef(*manager_);
  }

private:
  // A thread unsafe internal listener manager.
  Network::InternalListenerManager* manager_{nullptr};
};
} // namespace InternalListener
} // namespace Extensions
} // namespace Envoy
