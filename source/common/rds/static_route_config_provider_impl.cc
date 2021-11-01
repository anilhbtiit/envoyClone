#include "source/common/rds/static_route_config_provider_impl.h"

namespace Envoy {
namespace Rds {

StaticRouteConfigProviderImpl::StaticRouteConfigProviderImpl(
    const Protobuf::Message& route_config_proto, ConfigTraits& config_traits,
    Server::Configuration::ServerFactoryContext& factory_context,
    RouteConfigProviderManager& route_config_provider_manager)
    : config_(config_traits.createConfig(route_config_proto)),
      route_config_proto_(config_traits.cloneProto(route_config_proto)),
      route_config_name_(config_traits.resourceName(*route_config_proto_)),
      last_updated_(factory_context.timeSource().systemTime()),
      route_config_provider_manager_(route_config_provider_manager) {}

StaticRouteConfigProviderImpl::~StaticRouteConfigProviderImpl() {
  route_config_provider_manager_.eraseStaticProvider(this);
}

absl::optional<RouteConfigProvider::ConfigInfo> StaticRouteConfigProviderImpl::configInfo() const {
  return RouteConfigProvider::ConfigInfo{*route_config_proto_, route_config_name_, ""};
}

} // namespace Rds
} // namespace Envoy
