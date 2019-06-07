#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

typedef std::function<void()> RedirectCB;

/**
 * A manager for tracking redirection errors on a per cluster basis, and calling registered
 * callbacks when the error rate exceeds a configurable threshold (while ensuring that a minimum
 * time passes between calling the callback).
 */
class RedirectionManager {
public:
  class Handle {
  public:
    virtual ~Handle() = default;
  };

  using HandlePtr = std::unique_ptr<Handle>;

  virtual ~RedirectionManager() = default;

  /**
   * Notifies the manager that a redirection error has been received for a given cluster.
   * @param cluster_name is the name of the cluster.
   * @return bool true if a cluster's registered callback with is scheduled
   * to be called from the main thread dispatcher, false otherwise.
   */
  virtual bool onRedirection(const std::string& cluster_name) PURE;

  /**
   * Register a cluster to be tracked by the manager (called by main thread only).
   * @param cluster_name is the name of the cluster.
   * @param min_time_between_triggering is the minimum amount of time that must pass between
   * callback invocations.
   * @param redirects_per_minute_threshold is the number of redirects in the last minute that must
   * be reached to consider calling the callback.
   * @param cb is the cluster callback function.
   * @return HandlePtr is a smart pointer to an opaque Handle that will unregister the cluster upon
   * destruction.
   */
  virtual HandlePtr registerCluster(const std::string& cluster_name,
                                    const std::chrono::milliseconds min_time_between_triggering,
                                    const uint32_t redirects_per_minute_threshold,
                                    const RedirectCB cb) PURE;

  /**
   * Unregister a cluster from the manager (called by main thread only).
   * @param cluster_name is the name of the cluster.
   */
  virtual void unregisterCluster(const std::string& cluster_name) PURE;
};

using RedirectionManagerSharedPtr = std::shared_ptr<RedirectionManager>;

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
