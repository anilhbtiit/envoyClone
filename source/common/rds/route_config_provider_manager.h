#pragma once

#include "envoy/rds/config_traits.h"
#include "envoy/rds/route_config_provider.h"

namespace Envoy {
namespace Rds {

/**
 * The RouteConfigProviderManager interface exposes helper functions for
 * RouteConfigProvider implementations
 */
class RouteConfigProviderManager {
public:
  virtual ~RouteConfigProviderManager() = default;

  virtual void eraseStaticProvider(RouteConfigProvider* provider) PURE;
  virtual void eraseDynamicProvider(uint64_t manager_identifier) PURE;

  virtual ProtoTraits& protoTraits() PURE;
};

} // namespace Rds
} // namespace Envoy
