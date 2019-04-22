#pragma once

#include <string>
#include <vector>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/exception.h"
#include "envoy/common/pure.h"
#include "envoy/stats/stats_macros.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

class SubscriptionCallbacks {
public:
  virtual ~SubscriptionCallbacks() = default;

  /**
   * Called when a configuration update is received.
   * @param resources vector of fetched resources corresponding to the configuration update.
   * @param version_info supplies the version information as supplied by the xDS discovery response.
   * @throw EnvoyException with reason if the configuration is rejected. Otherwise the configuration
   *        is accepted. Accepted configurations have their version_info reflected in subsequent
   *        requests.
   */
  virtual void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                              const std::string& version_info) PURE;

  // TODO(fredlas) it is a HACK that there are two of these. After delta CDS is merged,
  //               I intend to reimplement all state-of-the-world xDSes' use of onConfigUpdate
  //               in terms of this delta-style one (and remove the original).
  /**
   * Called when a delta configuration update is received.
   * @param added_resources resources newly added since the previous fetch.
   * @param removed_resources names of resources that this fetch instructed to be removed.
   * @param system_version_info aggregate response data "version", for debugging.
   * @throw EnvoyException with reason if the config changes are rejected. Otherwise the changes
   *        are accepted. Accepted changes have their version_info reflected in subsequent requests.
   */
  virtual void
  onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                 const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                 const std::string& system_version_info) PURE;

  /**
   * Called when either the Subscription is unable to fetch a config update or when onConfigUpdate
   * invokes an exception.
   * @param e supplies any exception data on why the fetch failed. May be nullptr.
   */
  virtual void onConfigUpdateFailed(const EnvoyException* e) PURE;

  /**
   * Obtain the "name" of a v2 API resource in a google.protobuf.Any, e.g. the route config name for
   * a RouteConfiguration, based on the underlying resource type.
   */
  virtual std::string resourceName(const ProtobufWkt::Any& resource) PURE;
};

/**
 * Common abstraction for subscribing to versioned config updates. This may be implemented via bidi
 * gRPC streams, periodic/long polling REST or inotify filesystem updates.
 */
class Subscription {
public:
  virtual ~Subscription() = default;

  /**
   * Start a configuration subscription asynchronously. This should be called once and will continue
   * to fetch throughout the lifetime of the Subscription object.
   * @param resources set of resource names to fetch.
   * @param callbacks the callbacks to be notified of configuration updates. The callback must not
   *        result in the deletion of the Subscription object.
   */
  virtual void start(const std::set<std::string>& resources, SubscriptionCallbacks& callbacks) PURE;

  /**
   * Update the resources to fetch.
   * @param resources vector of resource names to fetch. It's a (not unordered_)set so that it can
   * be passed to std::set_difference, which must be given sorted collections.
   */
  virtual void updateResources(const std::set<std::string>& update_to_these_names) PURE;
};

/**
 * Per subscription stats. @see stats_macros.h
 */
// clang-format off
#define ALL_SUBSCRIPTION_STATS(COUNTER, GAUGE) \
  COUNTER(update_attempt)                      \
  COUNTER(update_success)                      \
  COUNTER(update_failure)                      \
  COUNTER(update_rejected)                     \
  GAUGE(version)
// clang-format on

/**
 * Struct definition for per subscription stats. @see stats_macros.h
 */
struct SubscriptionStats {
  ALL_SUBSCRIPTION_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

} // namespace Config
} // namespace Envoy
