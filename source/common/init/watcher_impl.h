#pragma once

#include <functional>

#include "envoy/init/watcher.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Init {

/**
 * A watcher is just a glorified callback function, called by a target or a manager when
 * initialization completes.
 */
using ReadyFn = std::function<void()>;
using TargetAwareReadyFn = std::function<void(absl::string_view)>;

/**
 * A WatcherHandleImpl functions as a weak reference to a Watcher. It is how a TargetImpl safely
 * notifies a ManagerImpl that it has initialized, and likewise it's how ManagerImpl safely tells
 * its client that all registered targets have initialized, with no guarantees about the lifetimes
 * of the manager or client.
 */
class WatcherHandleImpl : public WatcherHandle, Logger::Loggable<Logger::Id::init> {
private:
  friend class WatcherImpl;

  // Ctor with std::function<void()> callback function.
  WatcherHandleImpl(absl::string_view handle_name, absl::string_view name,
                    std::weak_ptr<ReadyFn> fn);

public:
  // Init::WatcherHandle.
  bool ready() const override;

private:
  // Name of the handle (either the name of the target calling the manager, or the name of the
  // manager calling the client).
  const std::string handle_name_;

  // Name of the watcher (either the name of the manager, or the name of the client).
  const std::string name_;

  // The watcher's callback function, only called if the weak pointer can be "locked".
  const std::weak_ptr<ReadyFn> fn_;
};

/**
 * A WatcherImpl is an entity that listens for notifications that either an initialization target or
 * all targets registered with a manager have initialized. It can only be invoked through a
 * WatcherHandleImpl.
 */
class WatcherImpl : public Watcher, Logger::Loggable<Logger::Id::init> {
public:
  /**
   * @param name a human-readable watcher name, for logging / debugging.
   * @param fn a callback function to invoke when `ready` is called on the handle.
   */
  WatcherImpl(absl::string_view name, ReadyFn fn);
  ~WatcherImpl() override;

  // Init::Watcher.
  absl::string_view name() const override;
  WatcherHandlePtr createHandle(absl::string_view handle_name) const override;

private:
  // Human-readable name for logging.
  const std::string name_;

  // The callback function, called via WatcherHandleImpl by either the target or the manager.
  const std::shared_ptr<ReadyFn> fn_;
};

/**
 * A WatcherHandleImpl functions as a weak reference to a Watcher. It is how a TargetImpl safely
 * notifies a ManagerImpl that it has initialized, and likewise it's how ManagerImpl safely tells
 * its client that all registered targets have initialized, with no guarantees about the lifetimes
 * of the manager or client.
 *
 * We restrict the watcher_ inside ManagerImpl to be constructed with the 'TargetAwareReadyFn' fn so
 * that the init manager will get target name information when the watcher_ calls
 * 'onTargetSendName(target_name)' For any other purpose, a watcher can be constructed with either
 * Ctor. If you do not need a watcher to carry any string information such as the target_name, the
 * first type with 'ReadyFn' fn is enough.
 */
class TargetAwareWatcherHandleImpl : public WatcherHandle, Logger::Loggable<Logger::Id::init> {
private:
  friend class TargetAwareWatcherImpl;

  // Ctor with std::function<void(absl::string_view)> callback function.
  TargetAwareWatcherHandleImpl(absl::string_view handle_name, absl::string_view name,
                               std::weak_ptr<TargetAwareReadyFn> fn);

public:
  // Init::WatcherHandle.
  bool ready() const override;

private:
  // Name of the handle (either the name of the target calling the manager, or the name of the
  // manager calling the client).
  const std::string handle_name_;

  // Name of the watcher (either the name of the manager, or the name of the client).
  const std::string name_;

  // The watcher's callback function, only called if the weak pointer can be "locked".
  const std::weak_ptr<TargetAwareReadyFn> fn_;
};

/**
 * A TargetAwareWatcherImpl is a WatcherImpl which is specially designed for init manager's internal
 * watcher_. This watcher_ will monitor all the targets this init manager has added. The callback
 * function fn has a absl::string_view parameter to pass the target name to init manager.
 */
class TargetAwareWatcherImpl : public Watcher, Logger::Loggable<Logger::Id::init> {
public:
  /**
   * @param name a human-readable watcher name, for logging / debugging.
   * @param fn a callback function to invoke when `ready` is called on the handle.
   */
  TargetAwareWatcherImpl(absl::string_view name, TargetAwareReadyFn fn);
  ~TargetAwareWatcherImpl() override;

  // Init::Watcher.
  absl::string_view name() const override;
  WatcherHandlePtr createHandle(absl::string_view handle_name) const override;

private:
  // Human-readable name for logging.
  const std::string name_;

  // The callback function with target_name parameter, called via WatcherHandleImpl by either the
  // target or the manager.
  const std::shared_ptr<TargetAwareReadyFn> fn_;
};

} // namespace Init
} // namespace Envoy
