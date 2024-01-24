#include "source/extensions/filters/http/aws_request_signing/config.h"

#include <iterator>
#include <string>

#include "envoy/common/optref.h"
#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.h"
#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/common/aws/credentials_provider_impl.h"
#include "source/extensions/common/aws/region_provider_impl.h"
#include "source/extensions/common/aws/sigv4_signer_impl.h"
#include "source/extensions/common/aws/sigv4a_signer_impl.h"
#include "source/extensions/common/aws/utility.h"
#include "source/extensions/filters/http/aws_request_signing/aws_request_signing_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsRequestSigningFilter {

bool isARegionSet(std::string region) {
  for (const char& c : "*,") {
    if (region.find(c) != std::string::npos) {
      return true;
    }
  }
  return false;
}

SigningAlgorithm getSigningAlgorithm(
    const envoy::extensions::filters::http::aws_request_signing::v3::AwsRequestSigning& config) {
  using namespace envoy::extensions::filters::http::aws_request_signing::v3;
  auto& logger = Logger::Registry::getLog(Logger::Id::filter);

  switch (config.signing_algorithm()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case AwsRequestSigning_SigningAlgorithm_AWS_SIGV4:
    ENVOY_LOG_TO_LOGGER(logger, debug, "Signing Algorithm is SigV4");
    return SigningAlgorithm::SIGV4;

  case AwsRequestSigning_SigningAlgorithm_AWS_SIGV4A:
    ENVOY_LOG_TO_LOGGER(logger, debug, "Signing Algorithm is SigV4A");
    return SigningAlgorithm::SIGV4A;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

Http::FilterFactoryCb AwsRequestSigningFilterFactory::createFilterFactoryFromProtoTyped(
    const AwsRequestSigningProtoConfig& config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {

  auto& server_context = context.serverFactoryContext();

  std::string region;
  region = config.region();

  if (region.empty()) {
    ENVOY_LOG_MISC(debug, "creating region provider chain");

    auto region_provider = std::make_shared<Extensions::Common::Aws::RegionProviderChain>();
    absl::optional<std::string> regionopt = region_provider->getRegion();
    if (!regionopt.has_value()) {
      throw EnvoyException("Region string cannot be retrieved from configuration, environment or "
                           "profile/config files.");
    }
    region = regionopt.value();
  }

  auto credentials_provider =
      std::make_shared<Extensions::Common::Aws::DefaultCredentialsProviderChain>(
          server_context.api(), makeOptRef(server_context), region,
          Extensions::Common::Aws::Utility::fetchMetadata);
  const auto matcher_config = Extensions::Common::Aws::AwsSigningHeaderExclusionVector(
      config.match_excluded_headers().begin(), config.match_excluded_headers().end());

  std::unique_ptr<Extensions::Common::Aws::Signer> signer;

  if (getSigningAlgorithm(config) == SigningAlgorithm::SIGV4A) {
    if (config.region().empty()) {
      throw EnvoyException("Region parameter does not contain a SigV4A region set.");
    }
    // We use config.region here as environment or file store is not a valid region location for
    // AWS_SIGV4A
    signer = std::make_unique<Extensions::Common::Aws::SigV4ASignerImpl>(
        config.service_name(), config.region(), credentials_provider,
        server_context.mainThreadDispatcher().timeSource(), matcher_config);
  } else {
    // Verify that we have not specified a region set when using sigv4 algorithm
    if (isARegionSet(region)) {
      throw EnvoyException("SigV4 region string cannot contain wildcards or commas. Region sets "
                           "can be specified when using signing_algorithm: AWS_SIGV4A.");
    }
    signer = std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
        config.service_name(), region, credentials_provider,
        server_context.mainThreadDispatcher().timeSource(), matcher_config);
  }

  auto filter_config =
      std::make_shared<FilterConfigImpl>(std::move(signer), stats_prefix, context.scope(),
                                         config.host_rewrite(), config.use_unsigned_payload());
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter = std::make_shared<Filter>(filter_config);
    callbacks.addStreamDecoderFilter(filter);
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
AwsRequestSigningFilterFactory::createRouteSpecificFilterConfigTyped(
    const AwsRequestSigningProtoPerRouteConfig& per_route_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  std::string region;

  region = per_route_config.aws_request_signing().region();

  if (region.empty()) {
    ENVOY_LOG_MISC(debug, "creating region provider chain");

    auto region_provider = std::make_shared<Extensions::Common::Aws::RegionProviderChain>();
    absl::optional<std::string> regionopt = region_provider->getRegion();
    if (!regionopt.has_value()) {
      throw EnvoyException("Region string cannot be retrieved from configuration, environment or "
                           "profile/config files.");
    }
    region = regionopt.value();
  }

  auto credentials_provider =
      std::make_shared<Extensions::Common::Aws::DefaultCredentialsProviderChain>(
          context.api(), makeOptRef(context), region,
          Extensions::Common::Aws::Utility::fetchMetadata);

  const auto matcher_config = Extensions::Common::Aws::AwsSigningHeaderExclusionVector(
      per_route_config.aws_request_signing().match_excluded_headers().begin(),
      per_route_config.aws_request_signing().match_excluded_headers().end());
  std::unique_ptr<Extensions::Common::Aws::Signer> signer;

  if (getSigningAlgorithm(per_route_config.aws_request_signing()) == SigningAlgorithm::SIGV4A) {
    if (per_route_config.aws_request_signing().region().empty()) {
      throw EnvoyException("Region parameter does not contain a SigV4A region set.");
    }
    signer = std::make_unique<Extensions::Common::Aws::SigV4ASignerImpl>(
        per_route_config.aws_request_signing().service_name(),
        per_route_config.aws_request_signing().region(), credentials_provider,
        context.mainThreadDispatcher().timeSource(), matcher_config);
  } else {
    // Verify that we have not specified a region set when using sigv4 algorithm
    if (isARegionSet(region)) {
      throw EnvoyException("SigV4 region string cannot contain wildcards or commas. Region sets "
                           "can be specified when using signing_algorithm: AWS_SIGV4A.");
    }
    signer = std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
        per_route_config.aws_request_signing().service_name(), region, credentials_provider,
        context.mainThreadDispatcher().timeSource(), matcher_config);
  }

  return std::make_shared<const FilterConfigImpl>(
      std::move(signer), per_route_config.stat_prefix(), context.scope(),
      per_route_config.aws_request_signing().host_rewrite(),
      per_route_config.aws_request_signing().use_unsigned_payload());
}

/**
 * Static registration for the AWS request signing filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AwsRequestSigningFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace AwsRequestSigningFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
