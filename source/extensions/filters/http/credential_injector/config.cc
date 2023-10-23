#include "source/extensions/filters/http/credential_injector/config.h"

#include "source/common/config/utility.h"
#include "source/extensions/credentials/common/factory.h"
#include "source/extensions/filters/http/credential_injector/credential_injector_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {

using Envoy::Extensions::Credentials::Common::NamedCredentialInjectorConfigFactory;
using envoy::extensions::filters::http::credential_injector::v3::CredentialInjector;

Http::FilterFactoryCb CredentialInjectorFilterFactory::createFilterFactoryFromProtoTyped(
    const CredentialInjector& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  // find the credential injector factory
  const std::string type{
      TypeUtil::typeUrlToDescriptorFullName(proto_config.credential().typed_config().type_url())};
  NamedCredentialInjectorConfigFactory* const config_factory =
      Registry::FactoryRegistry<NamedCredentialInjectorConfigFactory>::getFactoryByType(type);
  if (config_factory == nullptr) {
    throw EnvoyException(
        fmt::format("Didn't find a registered implementation for type: '{}'", type));
  }

  // create the credential injector
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      proto_config.credential().typed_config(), context.messageValidationVisitor(),
      *config_factory);
  CredentialInjectorSharedPtr credential_injector =
      config_factory->createCredentialInjectorFromProto(*message, context);

  FilterConfigSharedPtr config = std::make_shared<FilterConfig>(
      credential_injector, proto_config.overwrite(), stats_prefix, context.scope());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<CredentialInjectorFilter>(config));
  };
}

REGISTER_FACTORY(CredentialInjectorFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
