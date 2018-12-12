#include "extensions/filters/network/tcp_proxy/config.h"

#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/tcp_proxy/tcp_proxy.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpProxy {

Network::FilterFactoryCb
ConfigFactory::createFilterFactory(const Json::Object& json_config,
                                   Server::Configuration::FactoryContext& context) {
  envoy::config::filter::network::tcp_proxy::v2::TcpProxy proto_config;
  Config::FilterJson::translateTcpProxy(json_config, proto_config);
  return createFilterFactoryFromProtoTyped(proto_config, context);
}

Network::FilterFactoryCb ConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::tcp_proxy::v2::TcpProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.stat_prefix().empty());

  if (proto_config.cluster().empty() && (!proto_config.has_weighted_clusters() ||
                                         proto_config.weighted_clusters().clusters_size() == 0)) {
    throw EnvoyException("tcp_proxy: no cluster is defined in config");
  }

  Envoy::TcpProxy::ConfigSharedPtr filter_config(
      std::make_shared<Envoy::TcpProxy::Config>(proto_config, context));
  return [filter_config, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<Envoy::TcpProxy::Filter>(
        filter_config, context.clusterManager(), context.dispatcher().timeSystem()));
  };
}

/**
 * Static registration for the tcp_proxy filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<ConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace TcpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
