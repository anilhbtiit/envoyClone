#pragma once

#include <string>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth {

/**
 * Callback interface to enable the OAuth client to trigger actions upon completion of an
 * asynchronous HTTP request/response.
 */
class OAuth2FilterCallbacks {
public:
  virtual ~OAuth2FilterCallbacks() = default;

  virtual void onGetAccessTokenSuccess(const std::string& access_token,
                                       std::chrono::seconds expires_in) PURE;

  virtual void sendUnauthorizedResponse() PURE;
};

} // namespace Oauth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
