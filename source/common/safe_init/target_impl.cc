#include "common/safe_init/target_impl.h"

namespace Envoy {
namespace SafeInit {

TargetHandleImpl::TargetHandleImpl(absl::string_view handle_name, absl::string_view name,
                                   std::weak_ptr<std::function<void(WatcherHandlePtr)>> fn)
    : handle_name_(handle_name), name_(name), fn_(std::move(fn)) {}

bool TargetHandleImpl::initialize(const Watcher& watcher) const {
  auto locked_fn(fn_.lock());
  if (locked_fn) {
    // If we can "lock" a shared pointer to the target's callback function, call it
    // with a new handle to the ManagerImpl's watcher that was passed in.
    ENVOY_LOG(debug, "{} initializing {}", handle_name_, name_);
    (*locked_fn)(watcher.createHandle(name_));
    return true;
  } else {
    // If not, the target was already destroyed.
    ENVOY_LOG(debug, "{} can't initialize {} (unavailable)", handle_name_, name_);
    return false;
  }
}

TargetImpl::TargetImpl(absl::string_view name)
    : name_(fmt::format("target {}", name)),
      fn_(std::make_shared<std::function<void(WatcherHandlePtr)>>(
          [this](WatcherHandlePtr watcher_handle) {
            // Initialization callback: save the ManagerImpl's watcher handle we were given and
            // start initialization.
            watcher_handle_ = std::move(watcher_handle);
            initialize();
          })) {}

TargetImpl::~TargetImpl() { ENVOY_LOG(debug, "{} destroyed", name_); }

absl::string_view TargetImpl::name() const { return name_; }

TargetHandlePtr TargetImpl::createHandle(absl::string_view handle_name) const {
  // Note: can't use std::make_unique here because TargetHandleImpl ctor is private.
  return std::unique_ptr<TargetHandle>(new TargetHandleImpl(
      handle_name, name_, std::weak_ptr<std::function<void(WatcherHandlePtr)>>(fn_)));
}

bool TargetImpl::ready() {
  if (watcher_handle_) {
    // If we have a handle for the ManagerImpl's watcher, signal it and then reset so it can't be
    // accidentally signaled again.
    bool result = watcher_handle_->ready();
    watcher_handle_.reset();
    return result;
  }
  return false;
}

} // namespace SafeInit
} // namespace Envoy
