#pragma once

#include <string>

#include "envoy/extensions/http/injected_credentials/oauth2/v3/oauth2.pb.h"
#include "envoy/extensions/http/injected_credentials/oauth2/v3/oauth2.pb.validate.h"
#include "envoy/stats/stats_macros.h"

#include "source/extensions/http/injected_credentials/common/credential.h"
#include "source/extensions/http/injected_credentials/common/secret_reader.h"
#include "source/extensions/http/injected_credentials/oauth2/oauth_client.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace OAuth2 {

/**
 * All Oauth2 access token provider stats. @see stats_macros.h
 */
#define ALL_OAUTH2_CLIENT_CREDENTIAL_TOKEN_PROVIDER_STATS(COUNTER)                                 \
  COUNTER(token_fetch_failed_on_client_secret)                                                     \
  COUNTER(token_fetch_failed_on_cluster_not_found)                                                 \
  COUNTER(token_fetch_failed_on_oauth_server_response)                                             \
  COUNTER(token_requested)                                                                         \
  COUNTER(token_fetched)

/**
 * Struct definition for Oauth2 access token provider stats. @see stats_macros.h
 */
struct TokenProviderStats {
  ALL_OAUTH2_CLIENT_CREDENTIAL_TOKEN_PROVIDER_STATS(GENERATE_COUNTER_STRUCT)
};

using envoy::extensions::http::injected_credentials::oauth2::v3::OAuth2;

class ThreadLocalOauth2ClientCredentialsToken : public ThreadLocal::ThreadLocalObject {
public:
  ThreadLocalOauth2ClientCredentialsToken(absl::string_view token) : token_(token) {}

  const std::string& token() const { return token_; };

private:
  std::string token_;
};

using ThreadLocalOauth2ClientCredentialsTokenSharedPtr =
    std::shared_ptr<ThreadLocalOauth2ClientCredentialsToken>;
class TokenReader {
public:
  virtual ~TokenReader() = default;
  virtual const std::string& token() const PURE;
};

using TokenReaderConstSharedPtr = std::shared_ptr<const TokenReader>;

class TokenProvider : public TokenReader,
                      public FilterCallbacks,
                      public Logger::Loggable<Logger::Id::credential_injector> {
public:
  TokenProvider(Common::SecretReaderConstSharedPtr secret_reader, ThreadLocal::SlotAllocator& tls,
                Upstream::ClusterManager& cm, const OAuth2& proto_config,
                Event::Dispatcher& dispatcher, const std::string& stats_prefix,
                Stats::Scope& scope);
  void asyncGetAccessToken();

  const ThreadLocalOauth2ClientCredentialsToken& threadLocal() const {
    return tls_->getTyped<ThreadLocalOauth2ClientCredentialsToken>();
  }

  // TokenReader
  const std::string& token() const override;

  // FilterCallbacks
  void onGetAccessTokenSuccess(const std::string& access_code, std::chrono::seconds) override;
  void onGetAccessTokenFailure() override;

private:
  static TokenProviderStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return TokenProviderStats{
        ALL_OAUTH2_CLIENT_CREDENTIAL_TOKEN_PROVIDER_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  std::string token_;
  const Common::SecretReaderConstSharedPtr secret_reader_;
  ThreadLocal::SlotPtr tls_;
  std::unique_ptr<OAuth2Client> oauth2_client_;
  std::string client_id_;
  Event::Dispatcher* dispatcher_;
  Event::TimerPtr timer_;
  TokenProviderStats stats_;
};

} // namespace OAuth2
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
