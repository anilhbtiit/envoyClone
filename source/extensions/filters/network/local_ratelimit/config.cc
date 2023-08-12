#include "source/extensions/filters/network/local_ratelimit/config.h"

#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.validate.h"

#include "source/extensions/filters/network/local_ratelimit/local_ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LocalRateLimitFilter {

Network::FilterFactoryCb LocalRateLimitConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit& proto_config,
    const Network::NetworkFilterMatcherSharedPtr& network_filter_matcher,
    Server::Configuration::FactoryContext& context) {
  ConfigSharedPtr filter_config(
      std::make_shared<Config>(proto_config, context.mainThreadDispatcher(), context.scope(),
                               context.runtime(), context.singletonManager()));
  return [network_filter_matcher, filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(network_filter_matcher, std::make_shared<Filter>(filter_config));
  };
}

/**
 * Static registration for the local rate limit filter. @see RegisterFactory.
 */
REGISTER_FACTORY(LocalRateLimitConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace LocalRateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
