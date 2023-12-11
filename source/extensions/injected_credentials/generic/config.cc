#include "source/extensions/injected_credentials/generic/config.h"

#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Extensions {
namespace InjectedCredentials {
namespace Generic {

Common::CredentialInjectorSharedPtr
GenericCredentialInjectorFactory::createCredentialInjectorFromProtoTyped(
    const Generic& config, Server::Configuration::FactoryContext& context) {
  const auto& credential_secret = config.credential();
  auto& server_context = context.serverFactoryContext();
  auto& cluster_manager = server_context.clusterManager();
  auto& secret_manager = cluster_manager.clusterManagerFactory().secretManager();
  auto& transport_socket_factory = context.getTransportSocketFactoryContext();
  auto secret_provider = Common::secretsProvider(credential_secret, secret_manager,
                                                 transport_socket_factory, context.initManager());
  if (secret_provider == nullptr) {
    throw EnvoyException("invalid credential secret configuration");
  }

  auto secret_reader =
      std::make_shared<Common::SDSSecretReader>(secret_provider, server_context.api());
  std::string header = config.header();
  if (header.empty()) {
    header = "Authorization";
  }
  return std::make_shared<GenericCredentialInjector>(header, secret_reader);
}

/**
 * Static registration for the basic auth credential injector. @see
 * NamedCredentialInjectorConfigFactory.
 */
REGISTER_FACTORY(
    GenericCredentialInjectorFactory,
    Envoy::Extensions::InjectedCredentials::Common::NamedCredentialInjectorConfigFactory);

} // namespace Generic
} // namespace InjectedCredentials
} // namespace Extensions
} // namespace Envoy
