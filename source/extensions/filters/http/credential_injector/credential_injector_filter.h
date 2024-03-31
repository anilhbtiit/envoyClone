#pragma once

#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/http/injected_credentials/common/credential.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {

/**
 * All Credential Injector filter stats. @see stats_macros.h
 */
#define ALL_CREDENTIAL_INJECTOR_STATS(COUNTER)                                                     \
  COUNTER(injected)                                                                                \
  COUNTER(failed)                                                                                  \
  COUNTER(already_exists)

/**
 * Struct definition for Credential Injector stats. @see stats_macros.h
 */
struct CredentialInjectorStats {
  ALL_CREDENTIAL_INJECTOR_STATS(GENERATE_COUNTER_STRUCT)
};

using Envoy::Extensions::Http::InjectedCredentials::Common::CredentialInjector;
using Envoy::Extensions::Http::InjectedCredentials::Common::CredentialInjectorSharedPtr;

/**
 * Configuration for the Credential Injector filter.
 */
class FilterConfig : public Logger::Loggable<Logger::Id::credential_injector> {
public:
  FilterConfig(CredentialInjectorSharedPtr, bool overwrite, bool allow_request_without_credential,
               const std::string& stats_prefix, Stats::Scope& scope);
  CredentialInjectorStats& stats() { return stats_; }

  CredentialInjector::RequestPtr requestCredential(CredentialInjector::Callbacks& callbacks) {
    return injector_->requestCredential(callbacks);
  }

  // Inject configured credential to the HTTP request header.
  // return should continue processing the request or not
  bool injectCredential(Envoy::Http::RequestHeaderMap& headers);

  bool allowRequestWithoutCredential() const { return allow_request_without_credential_; }

private:
  static CredentialInjectorStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return CredentialInjectorStats{
        ALL_CREDENTIAL_INJECTOR_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  CredentialInjectorSharedPtr injector_;
  const bool overwrite_;
  const bool allow_request_without_credential_;
  CredentialInjectorStats stats_;
};
using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

// The Envoy filter to inject credentials.
class CredentialInjectorFilter : public Envoy::Http::PassThroughDecoderFilter,
                                 public CredentialInjector::Callbacks,
                                 public Logger::Loggable<Logger::Id::credential_injector> {
public:
  CredentialInjectorFilter(FilterConfigSharedPtr config);

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Envoy::Http::FilterHeadersStatus decodeHeaders(Envoy::Http::RequestHeaderMap& headers,
                                                 bool) override;
  void setDecoderFilterCallbacks(Envoy::Http::StreamDecoderFilterCallbacks&) override;

  // CredentialInjector::Callbacks
  void onSuccess() override;
  void onFailure(const std::string& reason) override;

private:
  Envoy::Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Envoy::Http::RequestHeaderMap* request_headers_{};

  FilterConfigSharedPtr config_;

  // Tracks any outstanding in-flight credential requests, allowing us to cancel the request
  // if the filter ends before the request completes.
  CredentialInjector::RequestPtr in_flight_credential_request_;

  // Tracks whether we have stopped iteration to wait for the credential provider.
  bool stop_iteration_ = false;

  // Tracks whether we have initialized the credential provider.
  bool credential_init_ = false;
  // Tracks whether the credential provider has succeeded.
  bool credential_success_ = false;
};

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
