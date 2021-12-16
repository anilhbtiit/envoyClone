#include "source/common/router/rds_impl.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/config/api_version.h"
#include "source/common/config/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/route_config_update_receiver_impl.h"

namespace Envoy {
namespace Router {

RouteConfigProviderSharedPtr RouteConfigProviderUtil::create(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
        config,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator, Init::Manager& init_manager,
    const std::string& stat_prefix, RouteConfigProviderManager& route_config_provider_manager) {
  OptionalHttpFilters optional_http_filters;
  auto& filters = config.http_filters();
  for (const auto& filter : filters) {
    if (filter.is_optional()) {
      optional_http_filters.insert(filter.name());
    }
  }
  switch (config.route_specifier_case()) {
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      RouteSpecifierCase::kRouteConfig:
    return route_config_provider_manager.createStaticRouteConfigProvider(
        config.route_config(), optional_http_filters, factory_context, validator);
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      RouteSpecifierCase::kRds:
    return route_config_provider_manager.createRdsRouteConfigProvider(
        // At the creation of a RDS route config provider, the factory_context's initManager is
        // always valid, though the init manager may go away later when the listener goes away.
        config.rds(), optional_http_filters, factory_context, stat_prefix, init_manager);
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

StaticRouteConfigProviderImpl::StaticRouteConfigProviderImpl(
    const envoy::config::route::v3::RouteConfiguration& config, Rds::ConfigTraits& config_traits,
    Server::Configuration::ServerFactoryContext& factory_context,
    Rds::RouteConfigProviderManager& route_config_provider_manager)
    : base_(config, config_traits, factory_context, route_config_provider_manager),
      route_config_provider_manager_(route_config_provider_manager) {}

StaticRouteConfigProviderImpl::~StaticRouteConfigProviderImpl() {
  route_config_provider_manager_.eraseStaticProvider(this);
}

ConfigConstSharedPtr StaticRouteConfigProviderImpl::configCast() {
  ASSERT(dynamic_cast<const Config*>(base_.config().get()));
  return std::static_pointer_cast<const Config>(base_.config());
}

// TODO(htuch): If support for multiple clusters is added per #1170 cluster_name_
RdsRouteConfigSubscription::RdsRouteConfigSubscription(
    RouteConfigUpdateReceiver* config_update,
    Envoy::Config::OpaqueResourceDecoder* resource_decoder,
    const envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
    const uint64_t manager_identifier, Server::Configuration::ServerFactoryContext& factory_context,
    const std::string& stat_prefix, Rds::RouteConfigProviderManager& route_config_provider_manager)
    : Rds::RdsRouteConfigSubscription(
          RouteConfigUpdatePtr(config_update),
          std::unique_ptr<Envoy::Config::OpaqueResourceDecoder>(resource_decoder),
          rds.config_source(), rds.route_config_name(), manager_identifier, factory_context,
          stat_prefix + "rds.", "RDS", route_config_provider_manager),
      config_update_info_(config_update) {}

void RdsRouteConfigSubscription::beforeProviderUpdate() {
  if (config_update_info_->protobufConfigurationCast().has_vhds() &&
      config_update_info_->vhdsConfigurationChanged()) {
    std::unique_ptr<Init::ManagerImpl> noop_init_manager;
    std::unique_ptr<Cleanup> resume_rds;
    ENVOY_LOG(debug,
              "rds: vhds configuration present/changed, (re)starting vhds: config_name={} hash={}",
              route_config_name_, config_update_info_->configHash());
    maybeCreateInitManager(config_update_info_->configVersion(), noop_init_manager, resume_rds);
    vhds_subscription_ = std::make_unique<VhdsSubscription>(
        config_update_info_, factory_context_, stat_prefix_, route_config_provider_opt_);
    vhds_subscription_->registerInitTargetWithInitManager(
        noop_init_manager == nullptr ? local_init_manager_ : *noop_init_manager);
  }
}

void RdsRouteConfigSubscription::afterProviderUpdate() {
  // RDS update removed VHDS configuration
  if (!config_update_info_->protobufConfigurationCast().has_vhds()) {
    vhds_subscription_.release();
  }

  update_callback_manager_.runCallbacks();
}

// Initialize a no-op InitManager in case the one in the factory_context has completed
// initialization. This can happen if an RDS config update for an already established RDS
// subscription contains VHDS configuration.
void RdsRouteConfigSubscription::maybeCreateInitManager(
    const std::string& version_info, std::unique_ptr<Init::ManagerImpl>& init_manager,
    std::unique_ptr<Cleanup>& init_vhds) {
  if (local_init_manager_.state() == Init::Manager::State::Initialized) {
    init_manager = std::make_unique<Init::ManagerImpl>(
        fmt::format("VHDS {}:{}", route_config_name_, version_info));
    init_vhds = std::make_unique<Cleanup>([this, &init_manager, version_info] {
      // For new RDS subscriptions created after listener warming up, we don't wait for them to warm
      // up.
      Init::WatcherImpl noop_watcher(
          // Note: we just throw it away.
          fmt::format("VHDS ConfigUpdate watcher {}:{}", route_config_name_, version_info),
          []() { /*Do nothing.*/ });
      init_manager->initialize(noop_watcher);
    });
  }
}

void RdsRouteConfigSubscription::updateOnDemand(const std::string& aliases) {
  if (vhds_subscription_.get() == nullptr) {
    return;
  }
  vhds_subscription_->updateOnDemand(aliases);
}

RdsRouteConfigProviderImpl::RdsRouteConfigProviderImpl(
    RdsRouteConfigSubscription* subscription,
    Server::Configuration::ServerFactoryContext& factory_context)
    : base_(RdsRouteConfigSubscriptionSharedPtr(subscription), factory_context),
      subscription_(subscription), config_update_info_(subscription->routeConfigUpdate()),
      factory_context_(factory_context) {
  // The subscription referenced by the 'base_' and by 'this' is the same.
  // But the subscription contains two references back to the provider.
  // One is in Rds::RdsRouteConfigSubscription other in RdsRouteConfigSubscription part.
  // The first is already initialized by the 'base_' so need to set back to 'this'.
  base_.subscription().routeConfigProvider().emplace(this);
  subscription_->routeConfigProvider().emplace(this);
}

RdsRouteConfigProviderImpl::~RdsRouteConfigProviderImpl() {
  ASSERT(subscription_->routeConfigProvider().has_value());
  subscription_->routeConfigProvider().reset();
}

void RdsRouteConfigProviderImpl::onConfigUpdate() {
  base_.onConfigUpdate();

  const auto aliases = config_update_info_->resourceIdsInLastVhdsUpdate();
  // Regular (non-VHDS) RDS updates don't populate aliases fields in resources.
  if (aliases.empty()) {
    return;
  }

  const auto config =
      std::static_pointer_cast<const ConfigImpl>(config_update_info_->parsedConfiguration());
  // Notifies connections that RouteConfiguration update has been propagated.
  // Callbacks processing is performed in FIFO order. The callback is skipped if alias used in
  // the VHDS update request do not match the aliases in the update response
  for (auto it = config_update_callbacks_.begin(); it != config_update_callbacks_.end();) {
    auto found = aliases.find(it->alias_);
    if (found != aliases.end()) {
      // TODO(dmitri-d) HeaderMapImpl is expensive, need to profile this
      auto host_header = Http::RequestHeaderMapImpl::create();
      host_header->setHost(VhdsSubscription::aliasToDomainName(it->alias_));
      const bool host_exists = config->virtualHostExists(*host_header);
      std::weak_ptr<Http::RouteConfigUpdatedCallback> current_cb(it->cb_);
      it->thread_local_dispatcher_.post([current_cb, host_exists] {
        if (auto cb = current_cb.lock()) {
          (*cb)(host_exists);
        }
      });
      it = config_update_callbacks_.erase(it);
    } else {
      it++;
    }
  }
}

ConfigConstSharedPtr RdsRouteConfigProviderImpl::configCast() {
  ASSERT(dynamic_cast<const Config*>(base_.config().get()));
  return std::static_pointer_cast<const Config>(base_.config());
}

// Schedules a VHDS request on the main thread and queues up the callback to use when the VHDS
// response has been propagated to the worker thread that was the request origin.
void RdsRouteConfigProviderImpl::requestVirtualHostsUpdate(
    const std::string& for_domain, Event::Dispatcher& thread_local_dispatcher,
    std::weak_ptr<Http::RouteConfigUpdatedCallback> route_config_updated_cb) {
  auto alias =
      VhdsSubscription::domainNameToAlias(config_update_info_->routeConfigName(), for_domain);
  // The RdsRouteConfigProviderImpl instance can go away before the dispatcher has a chance to
  // execute the callback. still_alive shared_ptr will be deallocated when the current instance of
  // the RdsRouteConfigProviderImpl is deallocated; we rely on a weak_ptr to still_alive flag to
  // determine if the RdsRouteConfigProviderImpl instance is still valid.
  factory_context_.mainThreadDispatcher().post([this,
                                                maybe_still_alive =
                                                    std::weak_ptr<bool>(still_alive_),
                                                alias, &thread_local_dispatcher,
                                                route_config_updated_cb]() -> void {
    if (maybe_still_alive.lock()) {
      subscription_->updateOnDemand(alias);
      config_update_callbacks_.push_back({alias, thread_local_dispatcher, route_config_updated_cb});
    }
  });
}

std::string ProtoTraitsImpl::resourceType() const {
  return Envoy::Config::getResourceName<envoy::config::route::v3::RouteConfiguration>();
}

ProtobufTypes::MessagePtr ProtoTraitsImpl::createEmptyProto() const {
  return std::make_unique<envoy::config::route::v3::RouteConfiguration>();
}

void ProtoTraitsImpl::validateResourceType(const Protobuf::Message& rc) const {
  auto dummy = &dynamic_cast<const envoy::config::route::v3::RouteConfiguration&>(rc);
  RELEASE_ASSERT(dummy, "");
}

const std::string& ProtoTraitsImpl::resourceName(const Protobuf::Message& rc) const {
  ASSERT(dynamic_cast<const envoy::config::route::v3::RouteConfiguration*>(&rc));
  return static_cast<const envoy::config::route::v3::RouteConfiguration&>(rc).name();
}

ProtobufTypes::MessagePtr ProtoTraitsImpl::cloneProto(const Protobuf::Message& rc) const {
  ASSERT(dynamic_cast<const envoy::config::route::v3::RouteConfiguration*>(&rc));
  return std::make_unique<envoy::config::route::v3::RouteConfiguration>(
      static_cast<const envoy::config::route::v3::RouteConfiguration&>(rc));
}

RouteConfigProviderManagerImpl::RouteConfigProviderManagerImpl(Server::Admin& admin)
    : manager_(admin, "routes", proto_traits_) {}

Router::RouteConfigProviderSharedPtr RouteConfigProviderManagerImpl::createRdsRouteConfigProvider(
    const envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
    const OptionalHttpFilters& optional_http_filters,
    Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
    Init::Manager& init_manager) {
  auto provider = manager_.addDynamicProvider(
      rds, rds.route_config_name(), init_manager,
      [&optional_http_filters, &factory_context, &rds, &stat_prefix,
       this](uint64_t manager_identifier) {
        auto config_update = new RouteConfigUpdateReceiverImpl(proto_traits_, factory_context,
                                                               optional_http_filters);
        auto resource_decoder = new Envoy::Config::OpaqueResourceDecoderImpl<
            envoy::config::route::v3::RouteConfiguration>(
            factory_context.messageValidationContext().dynamicValidationVisitor(), "name");
        auto subscription =
            new RdsRouteConfigSubscription(config_update, resource_decoder, rds, manager_identifier,
                                           factory_context, stat_prefix, manager_);
        auto provider = std::make_shared<RdsRouteConfigProviderImpl>(subscription, factory_context);
        return std::make_pair(provider, &provider->subscription().initTarget());
      });
  ASSERT(dynamic_cast<RouteConfigProvider*>(provider.get()));
  return std::static_pointer_cast<RouteConfigProvider>(provider);
}

RouteConfigProviderPtr RouteConfigProviderManagerImpl::createStaticRouteConfigProvider(
    const envoy::config::route::v3::RouteConfiguration& route_config,
    const OptionalHttpFilters& optional_http_filters,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator) {
  auto provider = manager_.addStaticProvider(
      [&optional_http_filters, &factory_context, &validator, &route_config, this]() {
        ConfigTraitsImpl config_traits(optional_http_filters, factory_context, validator, true);
        return std::make_unique<StaticRouteConfigProviderImpl>(route_config, config_traits,
                                                               factory_context, manager_);
      });
  ASSERT(dynamic_cast<RouteConfigProvider*>(provider.get()));
  return RouteConfigProviderPtr(static_cast<RouteConfigProvider*>(provider.release()));
}

} // namespace Router
} // namespace Envoy
