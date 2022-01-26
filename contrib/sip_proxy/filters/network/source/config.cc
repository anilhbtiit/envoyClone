#include "contrib/sip_proxy/filters/network/source/config.h"

#include <string>

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"

#include "contrib/envoy/extensions/filters/network/sip_proxy/v3alpha/sip_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/sip_proxy/v3alpha/sip_proxy.pb.validate.h"
#include "contrib/sip_proxy/filters/network/source/decoder.h"
#include "contrib/sip_proxy/filters/network/source/filters/filter_config.h"
#include "contrib/sip_proxy/filters/network/source/filters/well_known_names.h"
#include "contrib/sip_proxy/filters/network/source/router/router_impl.h"
#include "contrib/sip_proxy/filters/network/source/stats.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

namespace {
inline void
addUniqueClusters(absl::flat_hash_set<std::string>& clusters,
                  const envoy::extensions::filters::network::sip_proxy::v3alpha::Route& route) {
  clusters.emplace(route.route().cluster());
}
} // namespace

ProtocolOptionsConfigImpl::ProtocolOptionsConfigImpl(
    const envoy::extensions::filters::network::sip_proxy::v3alpha::SipProtocolOptions& config)
    : session_affinity_(config.session_affinity()),
      registration_affinity_(config.registration_affinity()) {

  for (const auto& affinity : config.customized_affinity()) {
    CustomizedAffinity aff(affinity.key_name(), affinity.query(), affinity.subscribe());
    customized_affinity_list_.emplace_back(aff);
  }
}

bool ProtocolOptionsConfigImpl::sessionAffinity() const { return session_affinity_; }
bool ProtocolOptionsConfigImpl::registrationAffinity() const { return registration_affinity_; }
const std::vector<CustomizedAffinity>& ProtocolOptionsConfigImpl::customizedAffinityList() const {
  return customized_affinity_list_;
}

Network::FilterFactoryCb SipProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  std::shared_ptr<Config> filter_config(new ConfigImpl(proto_config, context));

  absl::flat_hash_set<std::string> unique_clusters;
  for (auto& route : proto_config.route_config().routes()) {
    addUniqueClusters(unique_clusters, route);
  }

  /**
   * ConnPool::InstanceImpl contains ThreadLocalObject ThreadLocalPool which only can be
   * instantiated on main thread. so construct ConnPool::InstanceImpl here.
   */
  auto transaction_infos = std::make_shared<Router::TransactionInfos>();
  for (auto& cluster : unique_clusters) {
    Stats::ScopePtr stats_scope =
        context.scope().createScope(fmt::format("cluster.{}.sip_cluster", cluster));
    auto transaction_info_ptr = std::make_shared<Router::TransactionInfo>(
        cluster, context.threadLocal(),
        static_cast<std::chrono::milliseconds>(
            PROTOBUF_GET_MS_OR_DEFAULT(proto_config.settings(), transaction_timeout, 32000)),
        proto_config.settings().own_domain(),
        proto_config.settings().domain_match_parameter_name());
    transaction_info_ptr->init();
    transaction_infos->emplace(cluster, transaction_info_ptr);
  }

  return
      [filter_config, &context, transaction_infos](Network::FilterManager& filter_manager) -> void {
        filter_manager.addReadFilter(std::make_shared<ConnectionManager>(
            *filter_config, context.api().randomGenerator(),
            context.mainThreadDispatcher().timeSource(), context, transaction_infos));
      };
}

/**
 * Static registration for the sip filter. @see RegisterFactory.
 */
REGISTER_FACTORY(SipProxyFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

ConfigImpl::ConfigImpl(
    const envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy& config,
    Server::Configuration::FactoryContext& context)
    : context_(context), stats_prefix_(fmt::format("sip.{}.", config.stat_prefix())),
      stats_(SipFilterStats::generateStats(stats_prefix_, context_.scope())),
      route_matcher_(new Router::RouteMatcher(config.route_config())),
      settings_(std::make_shared<SipSettings>(
          static_cast<std::chrono::milliseconds>(
              PROTOBUF_GET_MS_OR_DEFAULT(config.settings(), transaction_timeout, 32000)),
          config.settings().own_domain(), config.settings().domain_match_parameter_name(),
          config.settings().tra_service_config())) {

  if (config.sip_filters().empty()) {
    ENVOY_LOG(debug, "using default router filter");

    envoy::extensions::filters::network::sip_proxy::v3alpha::SipFilter router;
    router.set_name(SipFilters::SipFilterNames::get().ROUTER);
    processFilter(router);
  } else {
    for (const auto& filter : config.sip_filters()) {
      processFilter(filter);
    }
  }
}

void ConfigImpl::createFilterChain(SipFilters::FilterChainFactoryCallbacks& callbacks) {
  for (const SipFilters::FilterFactoryCb& factory : filter_factories_) {
    factory(callbacks);
  }
}

void ConfigImpl::processFilter(
    const envoy::extensions::filters::network::sip_proxy::v3alpha::SipFilter& proto_config) {
  const std::string& string_name = proto_config.name();

  ENVOY_LOG(debug, "    sip filter #{}", filter_factories_.size());
  ENVOY_LOG(debug, "      name: {}", string_name);
  ENVOY_LOG(debug, "    config: {}",
            MessageUtil::getJsonStringFromMessageOrError(
                static_cast<const Protobuf::Message&>(proto_config.typed_config())));
  auto& factory =
      Envoy::Config::Utility::getAndCheckFactory<SipFilters::NamedSipFilterConfigFactory>(
          proto_config);

  ProtobufTypes::MessagePtr message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      proto_config.typed_config(), context_.messageValidationVisitor(), factory);
  SipFilters::FilterFactoryCb callback =
      factory.createFilterFactoryFromProto(*message, stats_prefix_, context_);

  filter_factories_.push_back(callback);
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
