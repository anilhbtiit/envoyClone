#pragma once

#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"
#include "envoy/stats/stats_macros.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

template <class ResourceType> class SubscriptionCallbacks {
public:
  typedef Protobuf::RepeatedPtrField<ResourceType> ResourceVector;

  virtual ~SubscriptionCallbacks() {}

  /**
   * Called when a configuration update is received.
   * @param resources vector of fetched resources corresponding to the configuration update.
   * @throw EnvoyException with reason if the configuration is rejected. Otherwise the configuration
   *        is accepted. Accepted configurations have their version_info reflected in subsequent
   *        requests.
   */
  virtual void onConfigUpdate(const ResourceVector& resources) PURE;

  /**
   * Called when either the Subscription is unable to fetch a config update or when onConfigUpdate
   * invokes an exception.
   * @param e supplies any exception data on why the fetch failed. May be nullptr.
   */
  virtual void onConfigUpdateFailed(const EnvoyException* e) PURE;
};

/**
 * Common abstraction for subscribing to versioned config updates. This may be implemented via bidi
 * gRPC streams, periodic/long polling REST or inotify filesystem updates. ResourceType is expected
 * to be a protobuf serializable object.
 */
template <class ResourceType> class Subscription {
public:
  virtual ~Subscription() {}

  /**
   * Start a configuration subscription asynchronously. This should be called once and will continue
   * to fetch throughout the lifetime of the Subscription object.
   * @param resources vector of resource names to fetch.
   * @param callbacks the callbacks to be notified of configuration updates. The callback must not
   *        result in the deletion of the Subscription object.
   */
  virtual void start(const std::vector<std::string>& resources,
                     SubscriptionCallbacks<ResourceType>& callbacks) PURE;

  /**
   * Update the resources to fetch.
   * @param resources vector of resource names to fetch.
   */
  virtual void updateResources(const std::vector<std::string>& resources) PURE;

  /**
   * @return version info from last accepted onConfigUpdate.
   */
  virtual const std::string& versionInfo() const PURE;
};

/**
 * Per subscription stats. @see stats_macros.h
 */
// clang-format off
#define ALL_SUBSCRIPTION_STATS(COUNTER) \
  COUNTER(update_attempt)               \
  COUNTER(update_success)               \
  COUNTER(update_failure)               \
  COUNTER(update_rejected)
// clang-format on

/**
 * Struct definition for per subscription stats. @see stats_macros.h
 */
struct SubscriptionStats {
  ALL_SUBSCRIPTION_STATS(GENERATE_COUNTER_STRUCT)
};

} // namespace Config
} // namespace Envoy
