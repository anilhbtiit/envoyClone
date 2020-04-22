#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

/**
 * Well-known internal redirect predidate names.
 */
class InternalRedirectPredicatesNameValues {
public:
  const std::string PreviousRoutesPredicate = "envoy.internal_redirect_predicates.previous_routes";
  const std::string WhitelistedRoutesPredicate =
      "envoy.internal_redirect_predicates.whitelisted_routes";
};

using InternalRedirectPredicateValues = ConstSingleton<InternalRedirectPredicatesNameValues>;

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
