#include "server/configuration_impl.h"

#include <chrono>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/instance.h"
#include "envoy/ssl/context_manager.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/config/lds_json.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"
#include "common/ratelimit/ratelimit_impl.h"
#include "common/tracing/http_tracer_impl.h"

#include "api/lds.pb.h"
#include "fmt/format.h"

namespace Envoy {
namespace Server {
namespace Configuration {

bool FilterChainUtility::buildFilterChain(Network::FilterManager& filter_manager,
                                          const std::vector<NetworkFilterFactoryCb>& factories) {
  for (const NetworkFilterFactoryCb& factory : factories) {
    factory(filter_manager);
  }

  return filter_manager.initializeReadFilters();
}

void MainImpl::initialize(const envoy::api::v2::Bootstrap& bootstrap, Instance& server,
                          Upstream::ClusterManagerFactory& cluster_manager_factory) {
  cluster_manager_ = cluster_manager_factory.clusterManagerFromProto(
      bootstrap, server.stats(), server.threadLocal(), server.runtime(), server.random(),
      server.localInfo(), server.accessLogManager());

  const auto& listeners = bootstrap.static_resources().listeners();
  ENVOY_LOG(info, "loading {} listener(s)", listeners.size());
  for (ssize_t i = 0; i < listeners.size(); i++) {
    ENVOY_LOG(info, "listener #{}:", i);
    server.listenerManager().addOrUpdateListener(listeners[i]);
  }

  if (bootstrap.dynamic_resources().has_lds_config()) {
    lds_api_.reset(new LdsApi(bootstrap.dynamic_resources().lds_config(), *cluster_manager_,
                              server.dispatcher(), server.random(), server.initManager(),
                              server.localInfo(), server.stats(), server.listenerManager()));
  }

  stats_flush_interval_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(bootstrap, stats_flush_interval, 5000));

  const auto& watchdog = bootstrap.watchdog();
  watchdog_miss_timeout_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(watchdog, miss_timeout, 200));
  watchdog_megamiss_timeout_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(watchdog, megamiss_timeout, 1000));
  watchdog_kill_timeout_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(watchdog, kill_timeout, 0));
  watchdog_multikill_timeout_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(watchdog, multikill_timeout, 0));

  initializeTracers(bootstrap.tracing(), server);

  if (bootstrap.has_rate_limit_service()) {
    ratelimit_client_factory_.reset(
        new RateLimit::GrpcFactoryImpl(bootstrap.rate_limit_service(), *cluster_manager_));
  } else {
    ratelimit_client_factory_.reset(new RateLimit::NullFactoryImpl());
  }

  initializeStatsSinks(bootstrap, server);
}

void MainImpl::initializeTracers(const envoy::api::v2::Tracing& configuration, Instance& server) {
  ENVOY_LOG(info, "loading tracing configuration");

  if (!configuration.has_http()) {
    http_tracer_.reset(new Tracing::HttpNullTracer());
    return;
  }

  if (server.localInfo().clusterName().empty()) {
    throw EnvoyException("cluster name must be defined if tracing is enabled. See "
                         "--service-cluster option.");
  }

  // Initialize tracing driver.
  std::string type = configuration.http().name();
  ENVOY_LOG(info, "  loading tracing driver: {}", type);

  // TODO(htuch): Make this dynamically pluggable one day.
  Json::ObjectSharedPtr driver_config =
      MessageUtil::getJsonObjectFromMessage(configuration.http().config());

  // Now see if there is a factory that will accept the config.
  HttpTracerFactory* factory = Registry::FactoryRegistry<HttpTracerFactory>::getFactory(type);
  if (factory != nullptr) {
    http_tracer_ = factory->createHttpTracer(*driver_config, server, *cluster_manager_);
  } else {
    throw EnvoyException(fmt::format("No HttpTracerFactory found for type: {}", type));
  }
}

void MainImpl::initializeStatsSinks(const envoy::api::v2::Bootstrap& bootstrap, Instance& server) {
  ENVOY_LOG(info, "loading stats sink configuration");

  for (const envoy::api::v2::StatsSink& sink_object : bootstrap.stats_sinks()) {
    if (sink_object.name().empty()) {
      throw EnvoyException(
          "sink object does not have 'name' attribute to look up the implementation");
    }

    ProtobufTypes::String name = sink_object.name();
    StatsSinkFactory* factory = Registry::FactoryRegistry<StatsSinkFactory>::getFactory(name);
    if (factory != nullptr) {
      ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
      if (sink_object.has_config()) {
        MessageUtil::jsonConvert(sink_object.config(), *message);
      }
      stats_sinks_.emplace_back(factory->createStatsSink(*message, server));
    } else {
      throw EnvoyException(fmt::format("No Stats::Sink found for name: {}", name));
    }
  }
}

InitialImpl::InitialImpl(const envoy::api::v2::Bootstrap& bootstrap) {
  const auto& admin = bootstrap.admin();
  admin_.access_log_path_ = admin.access_log_path();
  admin_.profile_path_ =
      admin.profile_path().empty() ? "/var/log/envoy/envoy.prof" : admin.profile_path();
  admin_.address_ = Network::Utility::fromProtoAddress(admin.address());

  if (!bootstrap.flags_path().empty()) {
    flags_path_.value(bootstrap.flags_path());
  }

  if (bootstrap.has_runtime()) {
    runtime_.reset(new RuntimeImpl());
    runtime_->symlink_root_ = bootstrap.runtime().symlink_root();
    runtime_->subdirectory_ = bootstrap.runtime().subdirectory();
    runtime_->override_subdirectory_ = bootstrap.runtime().override_subdirectory();
  }
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
