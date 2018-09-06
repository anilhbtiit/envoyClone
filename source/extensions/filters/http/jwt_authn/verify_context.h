#pragma once

#include <functional>

#include "envoy/http/header_map.h"

#include "extensions/filters/http/jwt_authn/authenticator.h"
#include "extensions/filters/http/jwt_authn/extractor.h"
#include "extensions/filters/http/jwt_authn/response_data.h"
#include "extensions/filters/http/jwt_authn/verifier_callbacks.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class VerifyContext;
typedef std::unique_ptr<VerifyContext> VerifyContextPtr;

/**
 * This object holds dynamic data generated on each request for verifiers.
 */
class VerifyContext {
public:
  virtual ~VerifyContext() {}

  /**
   * Returns the request headers wrapped in this context.
   *
   * @return the request headers.
   */
  virtual Http::HeaderMap& headers() const PURE;

  /**
   * Returns the original request callback wrapped in this context.
   *
   * @returns the original request callback.
   */
  virtual VerifierCallbacks* callback() const PURE;

  /**
   * Get Response data which can be used to check if a verifier node has responded or not.
   *
   * @param elem verifier node pointer.
   */
  virtual ResponseData& getResponseData(const void* elem) PURE;

  /**
   * Stores an authenticator object for this request.
   *
   * @param auth the authenticator object pointer.
   */
  virtual void addAuth(AuthenticatorPtr&& auth) PURE;

  /**
   * Cancel any pending reuqets for this context.
   */
  virtual void cancel() PURE;

  /**
   * Factory method for creating a new context object.
   */
  static VerifyContextPtr create(Http::HeaderMap& headers, VerifierCallbacks* callback);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
