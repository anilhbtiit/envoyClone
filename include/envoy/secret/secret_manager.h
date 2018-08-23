#pragma once

#include <string>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/secret/secret_provider.h"

namespace Envoy {

namespace Server {
namespace Configuration {
class TransportSocketFactoryContext;
} // namespace Configuration
} // namespace Server

namespace Secret {

/**
 * A manager for static and dynamic secrets.
 */
class SecretManager {
public:
  virtual ~SecretManager() {}

  /**
   * @param add a static secret from envoy::api::v2::auth::Secret.
   * @throw an EnvoyException if the secret is invalid or not supported, or there is duplicate.
   */
  virtual void addStaticSecret(const envoy::api::v2::auth::Secret& secret) PURE;

  /**
   * @param name a name of the static TlsCertificateConfigProvider.
   * @return the TlsCertificateConfigProviderSharedPtr. Returns nullptr if the static secret is not
   * found.
   */
  virtual TlsCertificateConfigProviderSharedPtr
  findStaticTlsCertificateProvider(const std::string& name) const PURE;

  /**
   * @param tls_certificate the protobuf config of the TLS certificate.
   * @return a TlsCertificateConfigProviderSharedPtr created from tls_certificate.
   */
  virtual TlsCertificateConfigProviderSharedPtr createInlineTlsCertificateProvider(
      const envoy::api::v2::auth::TlsCertificate& tls_certificate) PURE;

  /**
   * Finds and returns a dynamic secret provider associated to SDS config. Create
   * a new one if such provider does not exist.
   *
   * @param config_source a protobuf message object containing a SDS config source.
   * @param config_name a name that uniquely refers to the SDS config source.
   * @param secret_provider_context context that provides components for creating and initializing
   * secret provider.
   * @return TlsCertificateConfigProviderSharedPtr the dynamic TLS secret provider.
   */
  virtual TlsCertificateConfigProviderSharedPtr findOrCreateDynamicSecretProvider(
      const envoy::api::v2::core::ConfigSource& config_source, const std::string& config_name,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context) PURE;
};

} // namespace Secret
} // namespace Envoy
