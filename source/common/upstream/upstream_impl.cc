#include "source/common/upstream/upstream_impl.h"

#include <chrono>
#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/circuit_breaker.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"
#include "envoy/init/manager.h"
#include "envoy/network/dns.h"
#include "envoy/network/transport_socket.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/dns_utils.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/config/utility.h"
#include "source/common/http/http1/codec_stats.h"
#include "source/common/http/http2/codec_stats.h"
#include "source/common/http/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/happy_eyeballs_connection_impl.h"
#include "source/common/network/resolver_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/health_checker_impl.h"
#include "source/extensions/filters/network/http_connection_manager/config.h"
#include "source/server/transport_socket_config_impl.h"

#include "absl/container/node_hash_set.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Upstream {
namespace {

std::string addressToString(Network::Address::InstanceConstSharedPtr address) {
  if (!address) {
    return "";
  }
  return address->asString();
}

Network::TcpKeepaliveConfig
parseTcpKeepaliveConfig(const envoy::config::cluster::v3::Cluster& config) {
  const envoy::config::core::v3::TcpKeepalive& options =
      config.upstream_connection_options().tcp_keepalive();
  return Network::TcpKeepaliveConfig{
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(options, keepalive_probes, absl::optional<uint32_t>()),
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(options, keepalive_time, absl::optional<uint32_t>()),
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(options, keepalive_interval, absl::optional<uint32_t>())};
}

ProtocolOptionsConfigConstSharedPtr
createProtocolOptionsConfig(const std::string& name, const ProtobufWkt::Any& typed_config,
                            Server::Configuration::ProtocolOptionsFactoryContext& factory_context) {
  Server::Configuration::ProtocolOptionsFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::NamedNetworkFilterConfigFactory>::getFactory(
          name);
  if (factory == nullptr) {
    factory =
        Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
            name);
  }
  if (factory == nullptr) {
    factory =
        Registry::FactoryRegistry<Server::Configuration::ProtocolOptionsFactory>::getFactory(name);
  }

  if (factory == nullptr) {
    throw EnvoyException(fmt::format("Didn't find a registered network or http filter or protocol "
                                     "options implementation for name: '{}'",
                                     name));
  }

  ProtobufTypes::MessagePtr proto_config = factory->createEmptyProtocolOptionsProto();

  if (proto_config == nullptr) {
    throw EnvoyException(fmt::format("filter {} does not support protocol options", name));
  }

  Envoy::Config::Utility::translateOpaqueConfig(
      typed_config, factory_context.messageValidationVisitor(), *proto_config);
  return factory->createProtocolOptionsConfig(*proto_config, factory_context);
}

absl::flat_hash_map<std::string, ProtocolOptionsConfigConstSharedPtr> parseExtensionProtocolOptions(
    const envoy::config::cluster::v3::Cluster& config,
    Server::Configuration::ProtocolOptionsFactoryContext& factory_context) {
  absl::flat_hash_map<std::string, ProtocolOptionsConfigConstSharedPtr> options;

  for (const auto& it : config.typed_extension_protocol_options()) {
    auto& name = it.first;
    auto object = createProtocolOptionsConfig(name, it.second, factory_context);
    if (object != nullptr) {
      options[name] = std::move(object);
    }
  }

  return options;
}

// Updates the health flags for an existing host to match the new host.
// @param updated_host the new host to read health flag values from.
// @param existing_host the host to update.
// @param flag the health flag to update.
// @return bool whether the flag update caused the host health to change.
bool updateHealthFlag(const Host& updated_host, Host& existing_host, Host::HealthFlag flag) {
  // Check if the health flag has changed.
  if (existing_host.healthFlagGet(flag) != updated_host.healthFlagGet(flag)) {
    // Keep track of the previous health value of the host.
    const auto previous_health = existing_host.coarseHealth();

    if (updated_host.healthFlagGet(flag)) {
      existing_host.healthFlagSet(flag);
    } else {
      existing_host.healthFlagClear(flag);
    }

    // Rebuild if changing the flag affected the host health.
    return previous_health != existing_host.coarseHealth();
  }

  return false;
}

// Converts a set of hosts into a HostVector, excluding certain hosts.
// @param hosts hosts to convert
// @param excluded_hosts hosts to exclude from the resulting vector.
HostVector filterHosts(const absl::node_hash_set<HostSharedPtr>& hosts,
                       const absl::node_hash_set<HostSharedPtr>& excluded_hosts) {
  HostVector net_hosts;
  net_hosts.reserve(hosts.size());

  for (const auto& host : hosts) {
    if (excluded_hosts.find(host) == excluded_hosts.end()) {
      net_hosts.emplace_back(host);
    }
  }

  return net_hosts;
}

} // namespace

UpstreamLocalAddressSelectorImpl::UpstreamLocalAddressSelectorImpl(
    const envoy::config::cluster::v3::Cluster& cluster_config,
    const absl::optional<envoy::config::core::v3::BindConfig>& bootstrap_bind_config) {
  base_socket_options_ = buildBaseSocketOptions(
      cluster_config, bootstrap_bind_config.value_or(envoy::config::core::v3::BindConfig{}));
  cluster_socket_options_ = buildClusterSocketOptions(
      cluster_config, bootstrap_bind_config.value_or(envoy::config::core::v3::BindConfig{}));

  ASSERT(base_socket_options_ != nullptr);
  ASSERT(cluster_socket_options_ != nullptr);

  if (cluster_config.has_upstream_bind_config()) {
    parseBindConfig(cluster_config.name(), cluster_config.upstream_bind_config(),
                    base_socket_options_, cluster_socket_options_);
  } else if (bootstrap_bind_config.has_value()) {
    parseBindConfig("", *bootstrap_bind_config, base_socket_options_, cluster_socket_options_);
  }
}

Network::ConnectionSocket::OptionsSharedPtr
UpstreamLocalAddressSelectorImpl::combineConnectionSocketOptions(
    const Network::ConnectionSocket::OptionsSharedPtr& local_address_options,
    const Network::ConnectionSocket::OptionsSharedPtr& options) const {
  Network::ConnectionSocket::OptionsSharedPtr connection_options =
      std::make_shared<Network::ConnectionSocket::Options>();

  if (options) {
    connection_options = std::make_shared<Network::ConnectionSocket::Options>();
    *connection_options = *options;
    Network::Socket::appendOptions(connection_options, local_address_options);
  } else {
    *connection_options = *local_address_options;
  }

  return connection_options;
}

UpstreamLocalAddress UpstreamLocalAddressSelectorImpl::getUpstreamLocalAddress(
    const Network::Address::InstanceConstSharedPtr& endpoint_address,
    const Network::ConnectionSocket::OptionsSharedPtr& socket_options) const {
  // If there is no upstream local address specified, then return a nullptr for the address. And
  // return the socket options.
  if (upstream_local_addresses_.empty()) {
    UpstreamLocalAddress local_address;
    local_address.address_ = nullptr;
    local_address.socket_options_ = std::make_shared<Network::ConnectionSocket::Options>();
    Network::Socket::appendOptions(local_address.socket_options_, base_socket_options_);
    Network::Socket::appendOptions(local_address.socket_options_, cluster_socket_options_);
    local_address.socket_options_ =
        combineConnectionSocketOptions(local_address.socket_options_, socket_options);
    return local_address;
  }

  for (auto& local_address : upstream_local_addresses_) {
    if (local_address.address_ == nullptr) {
      continue;
    }

    ASSERT(local_address.address_->ip() != nullptr);
    if (endpoint_address->ip() != nullptr &&
        local_address.address_->ip()->version() == endpoint_address->ip()->version()) {
      return {local_address.address_,
              combineConnectionSocketOptions(local_address.socket_options_, socket_options)};
    }
  }

  return {
      upstream_local_addresses_[0].address_,
      combineConnectionSocketOptions(upstream_local_addresses_[0].socket_options_, socket_options)};
}

const Network::ConnectionSocket::OptionsSharedPtr
UpstreamLocalAddressSelectorImpl::buildBaseSocketOptions(
    const envoy::config::cluster::v3::Cluster& cluster_config,
    const envoy::config::core::v3::BindConfig& bootstrap_bind_config) {
  Network::ConnectionSocket::OptionsSharedPtr base_options =
      std::make_shared<Network::ConnectionSocket::Options>();

  // The process-wide `signal()` handling may fail to handle SIGPIPE if overridden
  // in the process (i.e., on a mobile client). Some OSes support handling it at the socket layer:
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    Network::Socket::appendOptions(base_options,
                                   Network::SocketOptionFactory::buildSocketNoSigpipeOptions());
  }
  // Cluster IP_FREEBIND settings, when set, will override the cluster manager wide settings.
  if ((bootstrap_bind_config.freebind().value() &&
       !cluster_config.upstream_bind_config().has_freebind()) ||
      cluster_config.upstream_bind_config().freebind().value()) {
    Network::Socket::appendOptions(base_options,
                                   Network::SocketOptionFactory::buildIpFreebindOptions());
  }
  if (cluster_config.upstream_connection_options().has_tcp_keepalive()) {
    Network::Socket::appendOptions(base_options,
                                   Network::SocketOptionFactory::buildTcpKeepaliveOptions(
                                       parseTcpKeepaliveConfig(cluster_config)));
  }

  return base_options;
}

const Network::ConnectionSocket::OptionsSharedPtr
UpstreamLocalAddressSelectorImpl::buildClusterSocketOptions(
    const envoy::config::cluster::v3::Cluster& cluster_config,
    const envoy::config::core::v3::BindConfig bind_config) {
  Network::ConnectionSocket::OptionsSharedPtr cluster_options =
      std::make_shared<Network::ConnectionSocket::Options>();
  // Cluster socket_options trump cluster manager wide.
  if (bind_config.socket_options().size() +
          cluster_config.upstream_bind_config().socket_options().size() >
      0) {
    auto socket_options = !cluster_config.upstream_bind_config().socket_options().empty()
                              ? cluster_config.upstream_bind_config().socket_options()
                              : bind_config.socket_options();
    Network::Socket::appendOptions(
        cluster_options, Network::SocketOptionFactory::buildLiteralOptions(socket_options));
  }
  return cluster_options;
}

void UpstreamLocalAddressSelectorImpl::parseBindConfig(
    const std::string cluster_name, const envoy::config::core::v3::BindConfig& bind_config,
    const Network::ConnectionSocket::OptionsSharedPtr& base_socket_options,
    const Network::ConnectionSocket::OptionsSharedPtr& cluster_socket_options) {
  if (bind_config.additional_source_addresses_size() > 0 &&
      bind_config.extra_source_addresses_size() > 0) {
    throw EnvoyException(
        fmt::format("Can't specify both `extra_source_addresses` and `additional_source_addresses` "
                    "in the {}'s upstream binding config",
                    cluster_name.empty() ? "Bootstrap" : fmt::format("Cluster {}", cluster_name)));
  }

  if (bind_config.extra_source_addresses_size() > 1) {
    throw EnvoyException(fmt::format(
        "{}'s upstream binding config has more than one extra source addresses. Only one "
        "extra source can be supported in BindConfig's extra_source_addresses field",
        cluster_name.empty() ? "Bootstrap" : fmt::format("Cluster {}", cluster_name)));
  }

  if (bind_config.additional_source_addresses_size() > 1) {
    throw EnvoyException(fmt::format(
        "{}'s upstream binding config has more than one additional source addresses. Only one "
        "additional source can be supported in BindConfig's additional_source_addresses field",
        cluster_name.empty() ? "Bootstrap" : fmt::format("Cluster {}", cluster_name)));
  }

  if (!bind_config.has_source_address() && (bind_config.extra_source_addresses_size() > 0 ||
                                            bind_config.additional_source_addresses_size() > 0)) {
    throw EnvoyException(
        fmt::format("{}'s upstream binding config has extra/additional source addresses but no "
                    "source_address. Extra/additional addresses cannot be specified if "
                    "source_address is not set.",
                    cluster_name.empty() ? "Bootstrap" : fmt::format("Cluster {}", cluster_name)));
  }

  UpstreamLocalAddress upstream_local_address;
  upstream_local_address.address_ =
      bind_config.has_source_address()
          ? Network::Address::resolveProtoSocketAddress(bind_config.source_address())
          : nullptr;
  upstream_local_address.socket_options_ = std::make_shared<Network::ConnectionSocket::Options>();

  Network::Socket::appendOptions(upstream_local_address.socket_options_, base_socket_options);
  Network::Socket::appendOptions(upstream_local_address.socket_options_, cluster_socket_options);

  upstream_local_addresses_.push_back(upstream_local_address);

  if (bind_config.extra_source_addresses_size() == 1) {
    UpstreamLocalAddress extra_upstream_local_address;
    extra_upstream_local_address.address_ = Network::Address::resolveProtoSocketAddress(
        bind_config.extra_source_addresses(0).address());
    ASSERT(extra_upstream_local_address.address_->ip() != nullptr &&
           upstream_local_address.address_->ip() != nullptr);
    if (extra_upstream_local_address.address_->ip()->version() ==
        upstream_local_address.address_->ip()->version()) {
      throw EnvoyException(fmt::format(
          "{}'s upstream binding config has two same IP version source addresses. Only two "
          "different IP version source addresses can be supported in BindConfig's source_address "
          "and extra_source_addresses fields",
          cluster_name.empty() ? "Bootstrap" : fmt::format("Cluster {}", cluster_name)));
    }

    extra_upstream_local_address.socket_options_ =
        std::make_shared<Network::ConnectionSocket::Options>();
    Network::Socket::appendOptions(extra_upstream_local_address.socket_options_,
                                   base_socket_options);

    if (bind_config.extra_source_addresses(0).has_socket_options()) {
      Network::Socket::appendOptions(
          extra_upstream_local_address.socket_options_,
          Network::SocketOptionFactory::buildLiteralOptions(
              bind_config.extra_source_addresses(0).socket_options().socket_options()));
    } else {
      Network::Socket::appendOptions(extra_upstream_local_address.socket_options_,
                                     cluster_socket_options);
    }

    upstream_local_addresses_.push_back(extra_upstream_local_address);
  }

  if (bind_config.additional_source_addresses_size() == 1) {
    UpstreamLocalAddress additional_upstream_local_address;
    additional_upstream_local_address.address_ =
        Network::Address::resolveProtoSocketAddress(bind_config.additional_source_addresses(0));
    ASSERT(additional_upstream_local_address.address_->ip() != nullptr &&
           upstream_local_address.address_->ip() != nullptr);
    if (additional_upstream_local_address.address_->ip()->version() ==
        upstream_local_address.address_->ip()->version()) {
      throw EnvoyException(fmt::format(
          "{}'s upstream binding config has two same IP version source addresses. Only two "
          "different IP version source addresses can be supported in BindConfig's source_address "
          "and additional_source_addresses fields",
          cluster_name.empty() ? "Bootstrap" : fmt::format("Cluster {}", cluster_name)));
    }

    additional_upstream_local_address.socket_options_ =
        std::make_shared<Network::ConnectionSocket::Options>();

    Network::Socket::appendOptions(additional_upstream_local_address.socket_options_,
                                   base_socket_options);
    Network::Socket::appendOptions(additional_upstream_local_address.socket_options_,
                                   cluster_socket_options);

    upstream_local_addresses_.push_back(additional_upstream_local_address);
  }
}

// TODO(pianiststickman): this implementation takes a lock on the hot path and puts a copy of the
// stat name into every host that receives a copy of that metric. This can be improved by putting
// a single copy of the stat name into a thread-local key->index map so that the lock can be avoided
// and using the index as the key to the stat map instead.
void LoadMetricStatsImpl::add(const absl::string_view key, double value) {
  absl::MutexLock lock(&mu_);
  if (map_ == nullptr) {
    map_ = std::make_unique<StatMap>();
  }
  Stat& stat = (*map_)[key];
  ++stat.num_requests_with_metric;
  stat.total_metric_value += value;
}

LoadMetricStats::StatMapPtr LoadMetricStatsImpl::latch() {
  absl::MutexLock lock(&mu_);
  StatMapPtr latched = std::move(map_);
  map_ = nullptr;
  return latched;
}

HostDescriptionImpl::HostDescriptionImpl(
    ClusterInfoConstSharedPtr cluster, const std::string& hostname,
    Network::Address::InstanceConstSharedPtr dest_address, MetadataConstSharedPtr metadata,
    const envoy::config::core::v3::Locality& locality,
    const envoy::config::endpoint::v3::Endpoint::HealthCheckConfig& health_check_config,
    uint32_t priority, TimeSource& time_source)
    : cluster_(cluster), hostname_(hostname),
      health_checks_hostname_(health_check_config.hostname()), address_(dest_address),
      canary_(Config::Metadata::metadataValue(metadata.get(),
                                              Config::MetadataFilters::get().ENVOY_LB,
                                              Config::MetadataEnvoyLbKeys::get().CANARY)
                  .bool_value()),
      metadata_(metadata), locality_(locality),
      locality_zone_stat_name_(locality.zone(), cluster->statsScope().symbolTable()),
      priority_(priority),
      socket_factory_(resolveTransportSocketFactory(dest_address, metadata_.get())),
      creation_time_(time_source.monotonicTime()) {
  if (health_check_config.port_value() != 0 && dest_address->type() != Network::Address::Type::Ip) {
    // Setting the health check port to non-0 only works for IP-type addresses. Setting the port
    // for a pipe address is a misconfiguration. Throw an exception.
    throw EnvoyException(
        fmt::format("Invalid host configuration: non-zero port for non-IP address"));
  }
  health_check_address_ = resolveHealthCheckAddress(health_check_config, dest_address);
}

Network::UpstreamTransportSocketFactory& HostDescriptionImpl::resolveTransportSocketFactory(
    const Network::Address::InstanceConstSharedPtr& dest_address,
    const envoy::config::core::v3::Metadata* metadata) const {
  auto match = cluster_->transportSocketMatcher().resolve(metadata);
  match.stats_.total_match_count_.inc();
  ENVOY_LOG(debug, "transport socket match, socket {} selected for host with address {}",
            match.name_, dest_address ? dest_address->asString() : "empty");

  return match.factory_;
}

Host::CreateConnectionData HostImpl::createConnection(
    Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
    Network::TransportSocketOptionsConstSharedPtr transport_socket_options) const {
  return createConnection(dispatcher, cluster(), address(), addressList(), transportSocketFactory(),
                          options, transport_socket_options, shared_from_this());
}

void HostImpl::setEdsHealthFlag(envoy::config::core::v3::HealthStatus health_status) {
  switch (health_status) {
  case envoy::config::core::v3::UNHEALTHY:
    FALLTHRU;
  case envoy::config::core::v3::DRAINING:
    FALLTHRU;
  case envoy::config::core::v3::TIMEOUT:
    healthFlagSet(Host::HealthFlag::FAILED_EDS_HEALTH);
    break;
  case envoy::config::core::v3::DEGRADED:
    healthFlagSet(Host::HealthFlag::DEGRADED_EDS_HEALTH);
    break;
  default:
    break;
    // No health flags should be set.
  }
}

Host::CreateConnectionData HostImpl::createHealthCheckConnection(
    Event::Dispatcher& dispatcher,
    Network::TransportSocketOptionsConstSharedPtr transport_socket_options,
    const envoy::config::core::v3::Metadata* metadata) const {

  Network::UpstreamTransportSocketFactory& factory =
      (metadata != nullptr) ? resolveTransportSocketFactory(healthCheckAddress(), metadata)
                            : transportSocketFactory();
  return createConnection(dispatcher, cluster(), healthCheckAddress(), {}, factory, nullptr,
                          transport_socket_options, shared_from_this());
}

Host::CreateConnectionData HostImpl::createConnection(
    Event::Dispatcher& dispatcher, const ClusterInfo& cluster,
    const Network::Address::InstanceConstSharedPtr& address,
    const std::vector<Network::Address::InstanceConstSharedPtr>& address_list,
    Network::UpstreamTransportSocketFactory& socket_factory,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    Network::TransportSocketOptionsConstSharedPtr transport_socket_options,
    HostDescriptionConstSharedPtr host) {
  auto source_address_selector = cluster.getUpstreamLocalAddressSelector();

  Network::ClientConnectionPtr connection;
  // If the transport socket options indicate the connection should be
  // redirected to a proxy, create the TCP connection to the proxy's address not
  // the host's address.
  if (transport_socket_options && transport_socket_options->http11ProxyInfo().has_value()) {
    ENVOY_LOG(debug, "Connecting to configured HTTP/1.1 proxy");
    auto upstream_local_address =
        source_address_selector->getUpstreamLocalAddress(address, options);
    connection = dispatcher.createClientConnection(
        transport_socket_options->http11ProxyInfo()->proxy_address, upstream_local_address.address_,
        socket_factory.createTransportSocket(transport_socket_options, host),
        upstream_local_address.socket_options_, transport_socket_options);
  } else if (address_list.size() > 1) {
    connection = std::make_unique<Network::HappyEyeballsConnectionImpl>(
        dispatcher, address_list, source_address_selector, socket_factory, transport_socket_options,
        host, options);
  } else {
    auto upstream_local_address =
        source_address_selector->getUpstreamLocalAddress(address, options);
    connection = dispatcher.createClientConnection(
        address, upstream_local_address.address_,
        socket_factory.createTransportSocket(transport_socket_options, host),
        upstream_local_address.socket_options_, transport_socket_options);
  }

  connection->connectionInfoSetter().enableSettingInterfaceName(
      cluster.setLocalInterfaceNameOnUpstreamConnections());
  connection->setBufferLimits(cluster.perConnectionBufferLimitBytes());
  cluster.createNetworkFilterChain(*connection);
  return {std::move(connection), std::move(host)};
}

void HostImpl::weight(uint32_t new_weight) { weight_ = std::max(1U, new_weight); }

std::vector<HostsPerLocalityConstSharedPtr> HostsPerLocalityImpl::filter(
    const std::vector<std::function<bool(const Host&)>>& predicates) const {
  // We keep two lists: one for being able to mutate the clone and one for returning to the
  // caller. Creating them both at the start avoids iterating over the mutable values at the end
  // to convert them to a const pointer.
  std::vector<std::shared_ptr<HostsPerLocalityImpl>> mutable_clones;
  std::vector<HostsPerLocalityConstSharedPtr> filtered_clones;

  for (size_t i = 0; i < predicates.size(); ++i) {
    mutable_clones.emplace_back(std::make_shared<HostsPerLocalityImpl>());
    filtered_clones.emplace_back(mutable_clones.back());
    mutable_clones.back()->local_ = local_;
  }

  for (const auto& hosts_locality : hosts_per_locality_) {
    std::vector<HostVector> current_locality_hosts;
    current_locality_hosts.resize(predicates.size());

    // Since # of hosts >> # of predicates, we iterate over the hosts in the outer loop.
    for (const auto& host : hosts_locality) {
      for (size_t i = 0; i < predicates.size(); ++i) {
        if (predicates[i](*host)) {
          current_locality_hosts[i].emplace_back(host);
        }
      }
    }

    for (size_t i = 0; i < predicates.size(); ++i) {
      mutable_clones[i]->hosts_per_locality_.push_back(std::move(current_locality_hosts[i]));
    }
  }

  return filtered_clones;
}

void HostSetImpl::updateHosts(PrioritySet::UpdateHostsParams&& update_hosts_params,
                              LocalityWeightsConstSharedPtr locality_weights,
                              const HostVector& hosts_added, const HostVector& hosts_removed,
                              absl::optional<uint32_t> overprovisioning_factor) {
  if (overprovisioning_factor.has_value()) {
    ASSERT(overprovisioning_factor.value() > 0);
    overprovisioning_factor_ = overprovisioning_factor.value();
  }
  hosts_ = std::move(update_hosts_params.hosts);
  healthy_hosts_ = std::move(update_hosts_params.healthy_hosts);
  degraded_hosts_ = std::move(update_hosts_params.degraded_hosts);
  excluded_hosts_ = std::move(update_hosts_params.excluded_hosts);
  hosts_per_locality_ = std::move(update_hosts_params.hosts_per_locality);
  healthy_hosts_per_locality_ = std::move(update_hosts_params.healthy_hosts_per_locality);
  degraded_hosts_per_locality_ = std::move(update_hosts_params.degraded_hosts_per_locality);
  excluded_hosts_per_locality_ = std::move(update_hosts_params.excluded_hosts_per_locality);
  locality_weights_ = std::move(locality_weights);

  rebuildLocalityScheduler(healthy_locality_scheduler_, healthy_locality_entries_,
                           *healthy_hosts_per_locality_, healthy_hosts_->get(), hosts_per_locality_,
                           excluded_hosts_per_locality_, locality_weights_,
                           overprovisioning_factor_);
  rebuildLocalityScheduler(degraded_locality_scheduler_, degraded_locality_entries_,
                           *degraded_hosts_per_locality_, degraded_hosts_->get(),
                           hosts_per_locality_, excluded_hosts_per_locality_, locality_weights_,
                           overprovisioning_factor_);

  runUpdateCallbacks(hosts_added, hosts_removed);
}

void HostSetImpl::rebuildLocalityScheduler(
    std::unique_ptr<EdfScheduler<LocalityEntry>>& locality_scheduler,
    std::vector<std::shared_ptr<LocalityEntry>>& locality_entries,
    const HostsPerLocality& eligible_hosts_per_locality, const HostVector& eligible_hosts,
    HostsPerLocalityConstSharedPtr all_hosts_per_locality,
    HostsPerLocalityConstSharedPtr excluded_hosts_per_locality,
    LocalityWeightsConstSharedPtr locality_weights, uint32_t overprovisioning_factor) {
  // Rebuild the locality scheduler by computing the effective weight of each
  // locality in this priority. The scheduler is reset by default, and is rebuilt only if we have
  // locality weights (i.e. using EDS) and there is at least one eligible host in this priority.
  //
  // We omit building a scheduler when there are zero eligible hosts in the priority as
  // all the localities will have zero effective weight. At selection time, we'll either select
  // from a different scheduler or there will be no available hosts in the priority. At that point
  // we'll rely on other mechanisms such as panic mode to select a host, none of which rely on the
  // scheduler.
  //
  // TODO(htuch): if the underlying locality index ->
  // envoy::config::core::v3::Locality hasn't changed in hosts_/healthy_hosts_/degraded_hosts_, we
  // could just update locality_weight_ without rebuilding. Similar to how host
  // level WRR works, we would age out the existing entries via picks and lazily
  // apply the new weights.
  locality_scheduler = nullptr;
  if (all_hosts_per_locality != nullptr && locality_weights != nullptr &&
      !locality_weights->empty() && !eligible_hosts.empty()) {
    locality_scheduler = std::make_unique<EdfScheduler<LocalityEntry>>();
    locality_entries.clear();
    for (uint32_t i = 0; i < all_hosts_per_locality->get().size(); ++i) {
      const double effective_weight = effectiveLocalityWeight(
          i, eligible_hosts_per_locality, *excluded_hosts_per_locality, *all_hosts_per_locality,
          *locality_weights, overprovisioning_factor);
      if (effective_weight > 0) {
        locality_entries.emplace_back(std::make_shared<LocalityEntry>(i, effective_weight));
        locality_scheduler->add(effective_weight, locality_entries.back());
      }
    }
    // If all effective weights were zero, reset the scheduler.
    if (locality_scheduler->empty()) {
      locality_scheduler = nullptr;
    }
  }
}

absl::optional<uint32_t> HostSetImpl::chooseHealthyLocality() {
  return chooseLocality(healthy_locality_scheduler_.get());
}

absl::optional<uint32_t> HostSetImpl::chooseDegradedLocality() {
  return chooseLocality(degraded_locality_scheduler_.get());
}

absl::optional<uint32_t>
HostSetImpl::chooseLocality(EdfScheduler<LocalityEntry>* locality_scheduler) {
  if (locality_scheduler == nullptr) {
    return {};
  }
  const std::shared_ptr<LocalityEntry> locality = locality_scheduler->pickAndAdd(
      [](const LocalityEntry& locality) { return locality.effective_weight_; });
  // We don't build a schedule if there are no weighted localities, so we should always succeed.
  ASSERT(locality != nullptr);
  // If we picked it before, its weight must have been positive.
  ASSERT(locality->effective_weight_ > 0);
  return locality->index_;
}

PrioritySet::UpdateHostsParams
HostSetImpl::updateHostsParams(HostVectorConstSharedPtr hosts,
                               HostsPerLocalityConstSharedPtr hosts_per_locality,
                               HealthyHostVectorConstSharedPtr healthy_hosts,
                               HostsPerLocalityConstSharedPtr healthy_hosts_per_locality,
                               DegradedHostVectorConstSharedPtr degraded_hosts,
                               HostsPerLocalityConstSharedPtr degraded_hosts_per_locality,
                               ExcludedHostVectorConstSharedPtr excluded_hosts,
                               HostsPerLocalityConstSharedPtr excluded_hosts_per_locality) {
  return PrioritySet::UpdateHostsParams{std::move(hosts),
                                        std::move(healthy_hosts),
                                        std::move(degraded_hosts),
                                        std::move(excluded_hosts),
                                        std::move(hosts_per_locality),
                                        std::move(healthy_hosts_per_locality),
                                        std::move(degraded_hosts_per_locality),
                                        std::move(excluded_hosts_per_locality)};
}

PrioritySet::UpdateHostsParams HostSetImpl::updateHostsParams(const HostSet& host_set) {
  return updateHostsParams(host_set.hostsPtr(), host_set.hostsPerLocalityPtr(),
                           host_set.healthyHostsPtr(), host_set.healthyHostsPerLocalityPtr(),
                           host_set.degradedHostsPtr(), host_set.degradedHostsPerLocalityPtr(),
                           host_set.excludedHostsPtr(), host_set.excludedHostsPerLocalityPtr());
}
PrioritySet::UpdateHostsParams
HostSetImpl::partitionHosts(HostVectorConstSharedPtr hosts,
                            HostsPerLocalityConstSharedPtr hosts_per_locality) {
  auto partitioned_hosts = ClusterImplBase::partitionHostList(*hosts);
  auto healthy_degraded_excluded_hosts_per_locality =
      ClusterImplBase::partitionHostsPerLocality(*hosts_per_locality);

  return updateHostsParams(std::move(hosts), std::move(hosts_per_locality),
                           std::move(std::get<0>(partitioned_hosts)),
                           std::move(std::get<0>(healthy_degraded_excluded_hosts_per_locality)),
                           std::move(std::get<1>(partitioned_hosts)),
                           std::move(std::get<1>(healthy_degraded_excluded_hosts_per_locality)),
                           std::move(std::get<2>(partitioned_hosts)),
                           std::move(std::get<2>(healthy_degraded_excluded_hosts_per_locality)));
}

double HostSetImpl::effectiveLocalityWeight(uint32_t index,
                                            const HostsPerLocality& eligible_hosts_per_locality,
                                            const HostsPerLocality& excluded_hosts_per_locality,
                                            const HostsPerLocality& all_hosts_per_locality,
                                            const LocalityWeights& locality_weights,
                                            uint32_t overprovisioning_factor) {
  const auto& locality_eligible_hosts = eligible_hosts_per_locality.get()[index];
  const uint32_t excluded_count = excluded_hosts_per_locality.get().size() > index
                                      ? excluded_hosts_per_locality.get()[index].size()
                                      : 0;
  const auto host_count = all_hosts_per_locality.get()[index].size() - excluded_count;
  if (host_count == 0) {
    return 0.0;
  }
  const double locality_availability_ratio = 1.0 * locality_eligible_hosts.size() / host_count;
  const uint32_t weight = locality_weights[index];
  // Availability ranges from 0-1.0, and is the ratio of eligible hosts to total hosts, modified
  // by the overprovisioning factor.
  const double effective_locality_availability_ratio =
      std::min(1.0, (overprovisioning_factor / 100.0) * locality_availability_ratio);
  return weight * effective_locality_availability_ratio;
}

const HostSet&
PrioritySetImpl::getOrCreateHostSet(uint32_t priority,
                                    absl::optional<uint32_t> overprovisioning_factor) {
  if (host_sets_.size() < priority + 1) {
    for (size_t i = host_sets_.size(); i <= priority; ++i) {
      HostSetImplPtr host_set = createHostSet(i, overprovisioning_factor);
      host_sets_priority_update_cbs_.push_back(
          host_set->addPriorityUpdateCb([this](uint32_t priority, const HostVector& hosts_added,
                                               const HostVector& hosts_removed) {
            runReferenceUpdateCallbacks(priority, hosts_added, hosts_removed);
          }));
      host_sets_.push_back(std::move(host_set));
    }
  }
  return *host_sets_[priority];
}

void PrioritySetImpl::updateHosts(uint32_t priority, UpdateHostsParams&& update_hosts_params,
                                  LocalityWeightsConstSharedPtr locality_weights,
                                  const HostVector& hosts_added, const HostVector& hosts_removed,
                                  absl::optional<uint32_t> overprovisioning_factor,
                                  HostMapConstSharedPtr cross_priority_host_map) {
  // Update cross priority host map first. In this way, when the update callbacks of the priority
  // set are executed, the latest host map can always be obtained.
  if (cross_priority_host_map != nullptr) {
    const_cross_priority_host_map_ = std::move(cross_priority_host_map);
  }

  // Ensure that we have a HostSet for the given priority.
  getOrCreateHostSet(priority, overprovisioning_factor);
  static_cast<HostSetImpl*>(host_sets_[priority].get())
      ->updateHosts(std::move(update_hosts_params), std::move(locality_weights), hosts_added,
                    hosts_removed, overprovisioning_factor);

  if (!batch_update_) {
    runUpdateCallbacks(hosts_added, hosts_removed);
  }
}

void PrioritySetImpl::batchHostUpdate(BatchUpdateCb& callback) {
  BatchUpdateScope scope(*this);

  // We wrap the update call with a lambda that tracks all the hosts that have been added/removed.
  callback.batchUpdate(scope);

  // Now that all the updates have been complete, we can compute the diff.
  HostVector net_hosts_added = filterHosts(scope.all_hosts_added_, scope.all_hosts_removed_);
  HostVector net_hosts_removed = filterHosts(scope.all_hosts_removed_, scope.all_hosts_added_);

  runUpdateCallbacks(net_hosts_added, net_hosts_removed);
}

void PrioritySetImpl::BatchUpdateScope::updateHosts(
    uint32_t priority, PrioritySet::UpdateHostsParams&& update_hosts_params,
    LocalityWeightsConstSharedPtr locality_weights, const HostVector& hosts_added,
    const HostVector& hosts_removed, absl::optional<uint32_t> overprovisioning_factor) {
  // We assume that each call updates a different priority.
  ASSERT(priorities_.find(priority) == priorities_.end());
  priorities_.insert(priority);

  for (const auto& host : hosts_added) {
    all_hosts_added_.insert(host);
  }

  for (const auto& host : hosts_removed) {
    all_hosts_removed_.insert(host);
  }

  parent_.updateHosts(priority, std::move(update_hosts_params), locality_weights, hosts_added,
                      hosts_removed, overprovisioning_factor);
}

void MainPrioritySetImpl::updateHosts(uint32_t priority, UpdateHostsParams&& update_hosts_params,
                                      LocalityWeightsConstSharedPtr locality_weights,
                                      const HostVector& hosts_added,
                                      const HostVector& hosts_removed,
                                      absl::optional<uint32_t> overprovisioning_factor,
                                      HostMapConstSharedPtr cross_priority_host_map) {
  ASSERT(cross_priority_host_map == nullptr,
         "External cross-priority host map is meaningless to MainPrioritySetImpl");
  updateCrossPriorityHostMap(hosts_added, hosts_removed);

  PrioritySetImpl::updateHosts(priority, std::move(update_hosts_params), locality_weights,
                               hosts_added, hosts_removed, overprovisioning_factor);
}

HostMapConstSharedPtr MainPrioritySetImpl::crossPriorityHostMap() const {
  // Check if the host set in the main thread PrioritySet has been updated.
  if (mutable_cross_priority_host_map_ != nullptr) {
    const_cross_priority_host_map_ = std::move(mutable_cross_priority_host_map_);
    ASSERT(mutable_cross_priority_host_map_ == nullptr);
  }
  return const_cross_priority_host_map_;
}

void MainPrioritySetImpl::updateCrossPriorityHostMap(const HostVector& hosts_added,
                                                     const HostVector& hosts_removed) {
  if (hosts_added.empty() && hosts_removed.empty()) {
    // No new hosts have been added and no old hosts have been removed.
    return;
  }

  // Since read_only_all_host_map_ may be shared by multiple threads, when the host set changes,
  // we cannot directly modify read_only_all_host_map_.
  if (mutable_cross_priority_host_map_ == nullptr) {
    // Copy old read only host map to mutable host map.
    mutable_cross_priority_host_map_ = std::make_shared<HostMap>(*const_cross_priority_host_map_);
  }

  for (const auto& host : hosts_removed) {
    mutable_cross_priority_host_map_->erase(addressToString(host->address()));
  }

  for (const auto& host : hosts_added) {
    mutable_cross_priority_host_map_->insert({addressToString(host->address()), host});
  }
}

LazyClusterTrafficStats ClusterInfoImpl::generateStats(Stats::Scope& scope,
                                                       const ClusterTrafficStatNames& stat_names) {
  return std::make_unique<ClusterTrafficStats>(stat_names, scope);
}

ClusterRequestResponseSizeStats ClusterInfoImpl::generateRequestResponseSizeStats(
    Stats::Scope& scope, const ClusterRequestResponseSizeStatNames& stat_names) {
  return ClusterRequestResponseSizeStats(stat_names, scope);
}

ClusterLoadReportStats
ClusterInfoImpl::generateLoadReportStats(Stats::Scope& scope,
                                         const ClusterLoadReportStatNames& stat_names) {
  return {stat_names, scope};
}

ClusterTimeoutBudgetStats
ClusterInfoImpl::generateTimeoutBudgetStats(Stats::Scope& scope,
                                            const ClusterTimeoutBudgetStatNames& stat_names) {
  return {stat_names, scope};
}

// Implements the FactoryContext interface required by network filters.
class FactoryContextImpl : public Server::Configuration::CommonFactoryContext {
public:
  // Create from a TransportSocketFactoryContext using parent stats_scope and runtime
  // other contexts taken from TransportSocketFactoryContext.
  FactoryContextImpl(Stats::Scope& stats_scope, Envoy::Runtime::Loader& runtime,
                     Server::Configuration::TransportSocketFactoryContext& c)
      : admin_(c.admin()), server_scope_(*c.stats().rootScope()), stats_scope_(stats_scope),
        cluster_manager_(c.clusterManager()), local_info_(c.localInfo()),
        dispatcher_(c.mainThreadDispatcher()), runtime_(runtime),
        singleton_manager_(c.singletonManager()), tls_(c.threadLocal()), api_(c.api()),
        options_(c.options()), message_validation_visitor_(c.messageValidationVisitor()) {}

  Upstream::ClusterManager& clusterManager() override { return cluster_manager_; }
  Event::Dispatcher& mainThreadDispatcher() override { return dispatcher_; }
  const Server::Options& options() override { return options_; }
  const LocalInfo::LocalInfo& localInfo() const override { return local_info_; }
  Envoy::Runtime::Loader& runtime() override { return runtime_; }
  Stats::Scope& scope() override { return stats_scope_; }
  Stats::Scope& serverScope() override { return server_scope_; }
  Singleton::Manager& singletonManager() override { return singleton_manager_; }
  ThreadLocal::SlotAllocator& threadLocal() override { return tls_; }
  OptRef<Server::Admin> admin() override { return admin_; }
  TimeSource& timeSource() override { return api().timeSource(); }
  ProtobufMessage::ValidationContext& messageValidationContext() override {
    // TODO(davinci26): Needs an implementation for this context. Currently not used.
    PANIC("unimplemented");
  }

  AccessLog::AccessLogManager& accessLogManager() override {
    // TODO(davinci26): Needs an implementation for this context. Currently not used.
    PANIC("unimplemented");
  }

  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return message_validation_visitor_;
  }

  Server::ServerLifecycleNotifier& lifecycleNotifier() override {
    // TODO(davinci26): Needs an implementation for this context. Currently not used.
    PANIC("unimplemented");
  }

  Init::Manager& initManager() override {
    // TODO(davinci26): Needs an implementation for this context. Currently not used.
    PANIC("unimplemented");
  }

  Api::Api& api() override { return api_; }

private:
  OptRef<Server::Admin> admin_;
  Stats::Scope& server_scope_;
  Stats::Scope& stats_scope_;
  Upstream::ClusterManager& cluster_manager_;
  const LocalInfo::LocalInfo& local_info_;
  Event::Dispatcher& dispatcher_;
  Envoy::Runtime::Loader& runtime_;
  Singleton::Manager& singleton_manager_;
  ThreadLocal::SlotAllocator& tls_;
  Api::Api& api_;
  const Server::Options& options_;
  ProtobufMessage::ValidationVisitor& message_validation_visitor_;
};

std::shared_ptr<const ClusterInfoImpl::HttpProtocolOptionsConfigImpl>
createOptions(const envoy::config::cluster::v3::Cluster& config,
              std::shared_ptr<const ClusterInfoImpl::HttpProtocolOptionsConfigImpl>&& options,
              ProtobufMessage::ValidationVisitor& validation_visitor) {
  if (options) {
    return std::move(options);
  }

  if (config.protocol_selection() == envoy::config::cluster::v3::Cluster::USE_CONFIGURED_PROTOCOL) {
    // Make sure multiple protocol configurations are not present
    if (config.has_http_protocol_options() && config.has_http2_protocol_options()) {
      throw EnvoyException(fmt::format("cluster: Both HTTP1 and HTTP2 options may only be "
                                       "configured with non-default 'protocol_selection' values"));
    }
  }

  return std::make_shared<ClusterInfoImpl::HttpProtocolOptionsConfigImpl>(
      config.http_protocol_options(), config.http2_protocol_options(),
      config.common_http_protocol_options(),
      (config.has_upstream_http_protocol_options()
           ? absl::make_optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>(
                 config.upstream_http_protocol_options())
           : absl::nullopt),
      config.protocol_selection() == envoy::config::cluster::v3::Cluster::USE_DOWNSTREAM_PROTOCOL,
      config.has_http2_protocol_options(), validation_visitor);
}

LBPolicyConfig::LBPolicyConfig(const envoy::config::cluster::v3::Cluster& config) {
  switch (config.lb_config_case()) {
  case envoy::config::cluster::v3::Cluster::kRoundRobinLbConfig:
    lbPolicy_ = std::make_unique<const envoy::config::cluster::v3::Cluster::RoundRobinLbConfig>(
        config.round_robin_lb_config());
    break;
  case envoy::config::cluster::v3::Cluster::kLeastRequestLbConfig:
    lbPolicy_ = std::make_unique<envoy::config::cluster::v3::Cluster::LeastRequestLbConfig>(
        config.least_request_lb_config());
    break;
  case envoy::config::cluster::v3::Cluster::kRingHashLbConfig:
    lbPolicy_ = std::make_unique<envoy::config::cluster::v3::Cluster::RingHashLbConfig>(
        config.ring_hash_lb_config());
    break;
  case envoy::config::cluster::v3::Cluster::kMaglevLbConfig:
    lbPolicy_ = std::make_unique<envoy::config::cluster::v3::Cluster::MaglevLbConfig>(
        config.maglev_lb_config());
    break;
  case envoy::config::cluster::v3::Cluster::kOriginalDstLbConfig:
    lbPolicy_ = std::make_unique<envoy::config::cluster::v3::Cluster::OriginalDstLbConfig>(
        config.original_dst_lb_config());
    break;
  case envoy::config::cluster::v3::Cluster::LB_CONFIG_NOT_SET:
    break;
  }
}

ClusterInfoImpl::ClusterInfoImpl(
    Init::Manager& init_manager, Server::Configuration::ServerFactoryContext& server_context,
    const envoy::config::cluster::v3::Cluster& config,
    const absl::optional<envoy::config::core::v3::BindConfig>& bind_config,
    Runtime::Loader& runtime, TransportSocketMatcherPtr&& socket_matcher,
    Stats::ScopeSharedPtr&& stats_scope, bool added_via_api,
    Server::Configuration::TransportSocketFactoryContext& factory_context)
    : runtime_(runtime), name_(config.name()),
      observability_name_(PROTOBUF_GET_STRING_OR_DEFAULT(config, alt_stat_name, name_)),
      extension_protocol_options_(parseExtensionProtocolOptions(config, factory_context)),
      http_protocol_options_(
          createOptions(config,
                        extensionProtocolOptionsTyped<HttpProtocolOptionsConfigImpl>(
                            "envoy.extensions.upstreams.http.v3.HttpProtocolOptions"),
                        factory_context.messageValidationVisitor())),
      tcp_protocol_options_(extensionProtocolOptionsTyped<TcpProtocolOptionsConfigImpl>(
          "envoy.extensions.upstreams.tcp.v3.TcpProtocolOptions")),
      max_requests_per_connection_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          http_protocol_options_->common_http_protocol_options_, max_requests_per_connection,
          config.max_requests_per_connection().value())),
      connect_timeout_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(config, connect_timeout, 5000))),
      per_upstream_preconnect_ratio_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          config.preconnect_policy(), per_upstream_preconnect_ratio, 1.0)),
      peekahead_ratio_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.preconnect_policy(),
                                                       predictive_preconnect_ratio, 0)),
      socket_matcher_(std::move(socket_matcher)), stats_scope_(std::move(stats_scope)),
      traffic_stats_(
          generateStats(*stats_scope_, factory_context.clusterManager().clusterStatNames())),
      config_update_stats_(factory_context.clusterManager().clusterConfigUpdateStatNames(),
                           *stats_scope_),
      lb_stats_(factory_context.clusterManager().clusterLbStatNames(), *stats_scope_),
      endpoint_stats_(factory_context.clusterManager().clusterEndpointStatNames(), *stats_scope_),
      load_report_stats_store_(stats_scope_->symbolTable()),
      load_report_stats_(
          generateLoadReportStats(*load_report_stats_store_.rootScope(),
                                  factory_context.clusterManager().clusterLoadReportStatNames())),
      optional_cluster_stats_((config.has_track_cluster_stats() || config.track_timeout_budgets())
                                  ? std::make_unique<OptionalClusterStats>(
                                        config, *stats_scope_, factory_context.clusterManager())
                                  : nullptr),
      features_(ClusterInfoImpl::HttpProtocolOptionsConfigImpl::parseFeatures(
          config, *http_protocol_options_)),
      resource_managers_(config, runtime, name_, *stats_scope_,
                         factory_context.clusterManager().clusterCircuitBreakersStatNames()),
      maintenance_mode_runtime_key_(absl::StrCat("upstream.maintenance_mode.", name_)),
      upstream_local_address_selector_(
          std::make_shared<UpstreamLocalAddressSelectorImpl>(config, bind_config)),
      lb_policy_config_(std::make_unique<const LBPolicyConfig>(config)),
      upstream_config_(config.has_upstream_config()
                           ? std::make_unique<envoy::config::core::v3::TypedExtensionConfig>(
                                 config.upstream_config())
                           : nullptr),
      lb_subset_(LoadBalancerSubsetInfoImpl(config.lb_subset_config())),
      metadata_(config.metadata()), typed_metadata_(config.metadata()),
      common_lb_config_(config.common_lb_config()),
      cluster_type_(config.has_cluster_type()
                        ? std::make_unique<envoy::config::cluster::v3::Cluster::CustomClusterType>(
                              config.cluster_type())
                        : nullptr),
      factory_context_(
          std::make_unique<FactoryContextImpl>(*stats_scope_, runtime, factory_context)),
      upstream_context_(server_context, init_manager, *stats_scope_),
      per_connection_buffer_limit_bytes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, per_connection_buffer_limit_bytes, 1024 * 1024)),
      max_response_headers_count_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          http_protocol_options_->common_http_protocol_options_, max_headers_count,
          runtime_.snapshot().getInteger(Http::MaxResponseHeadersCountOverrideKey,
                                         Http::DEFAULT_MAX_HEADERS_COUNT))),
      type_(config.type()),
      drain_connections_on_host_removal_(config.ignore_health_on_host_removal()),
      connection_pool_per_downstream_connection_(
          config.connection_pool_per_downstream_connection()),
      warm_hosts_(!config.health_checks().empty() &&
                  common_lb_config_.ignore_new_hosts_until_first_hc()),
      set_local_interface_name_on_upstream_connections_(
          config.upstream_connection_options().set_local_interface_name_on_upstream_connections()),
      added_via_api_(added_via_api), has_configured_http_filters_(false) {
#ifdef WIN32
  if (set_local_interface_name_on_upstream_connections_) {
    throw EnvoyException("set_local_interface_name_on_upstream_connections_ cannot be set to true "
                         "on Windows platforms");
  }
#endif

  if (config.has_max_requests_per_connection() &&
      http_protocol_options_->common_http_protocol_options_.has_max_requests_per_connection()) {
    throw EnvoyException("Only one of max_requests_per_connection from Cluster or "
                         "HttpProtocolOptions can be specified");
  }

  // If load_balancing_policy is set we will use it directly, ignoring lb_policy.
  if (config.has_load_balancing_policy()) {
    configureLbPolicies(config, server_context);
  } else {
    switch (config.lb_policy()) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case envoy::config::cluster::v3::Cluster::ROUND_ROBIN:
      lb_type_ = LoadBalancerType::RoundRobin;
      break;
    case envoy::config::cluster::v3::Cluster::LEAST_REQUEST:
      lb_type_ = LoadBalancerType::LeastRequest;
      break;
    case envoy::config::cluster::v3::Cluster::RANDOM:
      lb_type_ = LoadBalancerType::Random;
      break;
    case envoy::config::cluster::v3::Cluster::RING_HASH:
      lb_type_ = LoadBalancerType::RingHash;
      break;
    case envoy::config::cluster::v3::Cluster::MAGLEV:
      lb_type_ = LoadBalancerType::Maglev;
      break;
    case envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED:
      if (config.has_lb_subset_config()) {
        throw EnvoyException(
            fmt::format("cluster: LB policy {} cannot be combined with lb_subset_config",
                        envoy::config::cluster::v3::Cluster::LbPolicy_Name(config.lb_policy())));
      }

      lb_type_ = LoadBalancerType::ClusterProvided;
      break;
    case envoy::config::cluster::v3::Cluster::LOAD_BALANCING_POLICY_CONFIG: {
      configureLbPolicies(config, server_context);
      break;
    }
    }
  }

  if (config.lb_subset_config().locality_weight_aware() &&
      !config.common_lb_config().has_locality_weighted_lb_config()) {
    throw EnvoyException(fmt::format("Locality weight aware subset LB requires that a "
                                     "locality_weighted_lb_config be set in {}",
                                     name_));
  }

  if (http_protocol_options_->common_http_protocol_options_.has_idle_timeout()) {
    idle_timeout_ = std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
        http_protocol_options_->common_http_protocol_options_.idle_timeout()));
    if (idle_timeout_.value().count() == 0) {
      idle_timeout_ = absl::nullopt;
    }
  } else {
    idle_timeout_ = std::chrono::hours(1);
  }

  if (tcp_protocol_options_ && tcp_protocol_options_->idleTimeout().has_value()) {
    tcp_pool_idle_timeout_ = tcp_protocol_options_->idleTimeout();
    if (tcp_pool_idle_timeout_.value().count() == 0) {
      tcp_pool_idle_timeout_ = absl::nullopt;
    }
  } else {
    tcp_pool_idle_timeout_ = std::chrono::minutes(10);
  }

  if (http_protocol_options_->common_http_protocol_options_.has_max_connection_duration()) {
    max_connection_duration_ = std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
        http_protocol_options_->common_http_protocol_options_.max_connection_duration()));
    if (max_connection_duration_.value().count() == 0) {
      max_connection_duration_ = absl::nullopt;
    }
  } else {
    max_connection_duration_ = absl::nullopt;
  }

  if (config.has_eds_cluster_config()) {
    if (config.type() != envoy::config::cluster::v3::Cluster::EDS) {
      throw EnvoyException("eds_cluster_config set in a non-EDS cluster");
    }
    eds_service_name_ = config.eds_cluster_config().service_name();
  }

  // TODO(htuch): Remove this temporary workaround when we have
  // https://github.com/bufbuild/protoc-gen-validate/issues/97 resolved. This just provides
  // early validation of sanity of fields that we should catch at config ingestion.
  DurationUtil::durationToMilliseconds(common_lb_config_.update_merge_window());

  // Create upstream filter factories
  const auto& filters = config.filters();
  ASSERT(filter_factories_.empty());
  filter_factories_.reserve(filters.size());
  for (ssize_t i = 0; i < filters.size(); i++) {
    const auto& proto_config = filters[i];
    ENVOY_LOG(debug, "  upstream filter #{}:", i);
    ENVOY_LOG(debug, "    name: {}", proto_config.name());
    auto& factory = Config::Utility::getAndCheckFactory<
        Server::Configuration::NamedUpstreamNetworkFilterConfigFactory>(proto_config);
    auto message = factory.createEmptyConfigProto();
    Config::Utility::translateOpaqueConfig(proto_config.typed_config(),
                                           factory_context.messageValidationVisitor(), *message);
    Network::FilterFactoryCb callback =
        factory.createFilterFactoryFromProto(*message, *factory_context_);
    filter_factories_.push_back(std::move(callback));
  }

  if (http_protocol_options_) {
    Http::FilterChainUtility::FiltersList http_filters = http_protocol_options_->http_filters_;
    has_configured_http_filters_ = !http_filters.empty();
    if (http_filters.empty()) {
      auto* codec_filter = http_filters.Add();
      codec_filter->set_name("envoy.filters.http.upstream_codec");
      codec_filter->mutable_typed_config()->PackFrom(
          envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec::default_instance());
    }
    if (http_filters[http_filters.size() - 1].name() != "envoy.filters.http.upstream_codec") {
      throw EnvoyException(
          fmt::format("The codec filter is the only valid terminal upstream filter"));
    }
    std::shared_ptr<Http::UpstreamFilterConfigProviderManager> filter_config_provider_manager =
        Http::FilterChainUtility::createSingletonUpstreamFilterConfigProviderManager(
            upstream_context_.getServerFactoryContext());

    std::string prefix = stats_scope_->symbolTable().toString(stats_scope_->prefix());
    Http::FilterChainHelper<Server::Configuration::UpstreamHttpFactoryContext,
                            Server::Configuration::UpstreamHttpFilterConfigFactory>
        helper(*filter_config_provider_manager, upstream_context_.getServerFactoryContext(),
               upstream_context_, prefix);
    helper.processFilters(http_filters, "upstream http", "upstream http", http_filter_factories_);
  }
}

// Configures the load balancer based on config.load_balancing_policy
void ClusterInfoImpl::configureLbPolicies(const envoy::config::cluster::v3::Cluster& config,
                                          Server::Configuration::ServerFactoryContext& context) {
  // Check if load_balancing_policy is set first.
  if (!config.has_load_balancing_policy()) {
    throw EnvoyException("cluster: field load_balancing_policy need to be set");
  }

  if (config.has_lb_subset_config()) {
    throw EnvoyException("cluster: load_balancing_policy cannot be combined with lb_subset_config");
  }

  if (config.has_common_lb_config()) {
    const auto& lb_config = config.common_lb_config();
    if (lb_config.has_zone_aware_lb_config() || lb_config.has_locality_weighted_lb_config() ||
        lb_config.has_consistent_hashing_lb_config()) {
      throw EnvoyException(
          "cluster: load_balancing_policy cannot be combined with partial fields "
          "(zone_aware_lb_config, "
          "locality_weighted_lb_config, consistent_hashing_lb_config) of common_lb_config");
    }
  }

  absl::InlinedVector<absl::string_view, 4> missing_policies;
  for (const auto& policy : config.load_balancing_policy().policies()) {
    TypedLoadBalancerFactory* factory =
        Config::Utility::getAndCheckFactory<TypedLoadBalancerFactory>(
            policy.typed_extension_config(), /*is_optional=*/true);
    if (factory != nullptr) {
      // Load and validate the configuration.
      load_balancing_policy_ = factory->createEmptyConfigProto();
      Config::Utility::translateOpaqueConfig(policy.typed_extension_config().typed_config(),
                                             context.messageValidationVisitor(),
                                             *load_balancing_policy_);

      load_balancer_factory_ = factory;
      break;
    }
    missing_policies.push_back(policy.typed_extension_config().name());
  }

  if (load_balancer_factory_ == nullptr) {
    throw EnvoyException(fmt::format("cluster: didn't find a registered load balancer factory "
                                     "implementation for cluster: '{}' with names from [{}]",
                                     name_, absl::StrJoin(missing_policies, ", ")));
  }

  lb_type_ = LoadBalancerType::LoadBalancingPolicyConfig;
}

ProtocolOptionsConfigConstSharedPtr
ClusterInfoImpl::extensionProtocolOptions(const std::string& name) const {
  auto i = extension_protocol_options_.find(name);
  if (i != extension_protocol_options_.end()) {
    return i->second;
  }
  return nullptr;
}

Network::UpstreamTransportSocketFactoryPtr createTransportSocketFactory(
    const envoy::config::cluster::v3::Cluster& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context) {
  // If the cluster config doesn't have a transport socket configured, override with the default
  // transport socket implementation based on the tls_context. We copy by value first then
  // override if necessary.
  auto transport_socket = config.transport_socket();
  if (!config.has_transport_socket()) {
    envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer raw_buffer;
    transport_socket.mutable_typed_config()->PackFrom(raw_buffer);
    transport_socket.set_name("envoy.transport_sockets.raw_buffer");
  }

  auto& config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(transport_socket);
  ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
      transport_socket, factory_context.messageValidationVisitor(), config_factory);
  return config_factory.createTransportSocketFactory(*message, factory_context);
}

void ClusterInfoImpl::createNetworkFilterChain(Network::Connection& connection) const {
  for (const auto& factory : filter_factories_) {
    factory(connection);
  }
}

std::vector<Http::Protocol>
ClusterInfoImpl::upstreamHttpProtocol(absl::optional<Http::Protocol> downstream_protocol) const {
  if (downstream_protocol.has_value() &&
      features_ & Upstream::ClusterInfo::Features::USE_DOWNSTREAM_PROTOCOL) {
    if (downstream_protocol.value() == Http::Protocol::Http3 &&
        !(features_ & Upstream::ClusterInfo::Features::HTTP3)) {
      return {Http::Protocol::Http2};
    }
    return {downstream_protocol.value()};
  }

  if (features_ & Upstream::ClusterInfo::Features::USE_ALPN) {
    if (!(features_ & Upstream::ClusterInfo::Features::HTTP3)) {
      return {Http::Protocol::Http2, Http::Protocol::Http11};
    }
    return {Http::Protocol::Http3, Http::Protocol::Http2, Http::Protocol::Http11};
  }

  if (features_ & Upstream::ClusterInfo::Features::HTTP3) {
    return {Http::Protocol::Http3};
  }

  return {(features_ & Upstream::ClusterInfo::Features::HTTP2) ? Http::Protocol::Http2
                                                               : Http::Protocol::Http11};
}

ClusterImplBase::ClusterImplBase(
    Server::Configuration::ServerFactoryContext& server_context,
    const envoy::config::cluster::v3::Cluster& cluster, Runtime::Loader& runtime,
    Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
    Stats::ScopeSharedPtr&& stats_scope, bool added_via_api, TimeSource& time_source)
    : init_manager_(fmt::format("Cluster {}", cluster.name())),
      init_watcher_("ClusterImplBase", [this]() { onInitDone(); }), runtime_(runtime),
      wait_for_warm_on_init_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(cluster, wait_for_warm_on_init, true)),
      time_source_(time_source),
      local_cluster_(factory_context.clusterManager().localClusterName().value_or("") ==
                     cluster.name()),
      const_metadata_shared_pool_(Config::Metadata::getConstMetadataSharedPool(
          factory_context.singletonManager(), factory_context.mainThreadDispatcher())) {
  factory_context.setInitManager(init_manager_);
  auto socket_factory = createTransportSocketFactory(cluster, factory_context);
  auto* raw_factory_pointer = socket_factory.get();

  auto socket_matcher = std::make_unique<TransportSocketMatcherImpl>(
      cluster.transport_socket_matches(), factory_context, socket_factory, *stats_scope);
  const bool matcher_supports_alpn = socket_matcher->allMatchesSupportAlpn();
  auto& dispatcher = factory_context.mainThreadDispatcher();
  info_ = std::shared_ptr<const ClusterInfoImpl>(
      new ClusterInfoImpl(init_manager_, server_context, cluster,
                          factory_context.clusterManager().bindConfig(), runtime,
                          std::move(socket_matcher), std::move(stats_scope), added_via_api,
                          factory_context),
      [&dispatcher](const ClusterInfoImpl* self) {
        ENVOY_LOG(trace, "Schedule destroy cluster info {}", self->name());
        dispatcher.deleteInDispatcherThread(
            std::unique_ptr<const Event::DispatcherThreadDeletable>(self));
      });

  if ((info_->features() & ClusterInfoImpl::Features::USE_ALPN)) {
    if (!raw_factory_pointer->supportsAlpn()) {
      throw EnvoyException(
          fmt::format("ALPN configured for cluster {} which has a non-ALPN transport socket: {}",
                      cluster.name(), cluster.DebugString()));
    }
    if (!matcher_supports_alpn) {
      throw EnvoyException(fmt::format(
          "ALPN configured for cluster {} which has a non-ALPN transport socket matcher: {}",
          cluster.name(), cluster.DebugString()));
    }
  }

  if (info_->features() & ClusterInfoImpl::Features::HTTP3) {
#if defined(ENVOY_ENABLE_QUIC)
    if (cluster.transport_socket().DebugString().find("envoy.transport_sockets.quic") ==
        std::string::npos) {
      throw EnvoyException(
          fmt::format("HTTP3 requires a QuicUpstreamTransport transport socket: {}", cluster.name(),
                      cluster.DebugString()));
    }
#else
    throw EnvoyException("HTTP3 configured but not enabled in the build.");
#endif
  }

  // Create the default (empty) priority set before registering callbacks to
  // avoid getting an update the first time it is accessed.
  priority_set_.getOrCreateHostSet(0);
  priority_update_cb_ = priority_set_.addPriorityUpdateCb(
      [this](uint32_t, const HostVector& hosts_added, const HostVector& hosts_removed) {
        if (!hosts_added.empty() || !hosts_removed.empty()) {
          info_->endpointStats().membership_change_.inc();
        }

        uint32_t healthy_hosts = 0;
        uint32_t degraded_hosts = 0;
        uint32_t excluded_hosts = 0;
        uint32_t hosts = 0;
        for (const auto& host_set : prioritySet().hostSetsPerPriority()) {
          hosts += host_set->hosts().size();
          healthy_hosts += host_set->healthyHosts().size();
          degraded_hosts += host_set->degradedHosts().size();
          excluded_hosts += host_set->excludedHosts().size();
        }
        info_->endpointStats().membership_total_.set(hosts);
        info_->endpointStats().membership_healthy_.set(healthy_hosts);
        info_->endpointStats().membership_degraded_.set(degraded_hosts);
        info_->endpointStats().membership_excluded_.set(excluded_hosts);
      });
}

namespace {

bool excludeBasedOnHealthFlag(const Host& host) {
  return host.healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC) ||
         host.healthFlagGet(Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL);
}

} // namespace

std::tuple<HealthyHostVectorConstSharedPtr, DegradedHostVectorConstSharedPtr,
           ExcludedHostVectorConstSharedPtr>
ClusterImplBase::partitionHostList(const HostVector& hosts) {
  auto healthy_list = std::make_shared<HealthyHostVector>();
  auto degraded_list = std::make_shared<DegradedHostVector>();
  auto excluded_list = std::make_shared<ExcludedHostVector>();

  for (const auto& host : hosts) {
    if (host->coarseHealth() == Host::Health::Healthy) {
      healthy_list->get().emplace_back(host);
    }
    if (host->coarseHealth() == Host::Health::Degraded) {
      degraded_list->get().emplace_back(host);
    }
    if (excludeBasedOnHealthFlag(*host)) {
      excluded_list->get().emplace_back(host);
    }
  }

  return std::make_tuple(healthy_list, degraded_list, excluded_list);
}

std::tuple<HostsPerLocalityConstSharedPtr, HostsPerLocalityConstSharedPtr,
           HostsPerLocalityConstSharedPtr>
ClusterImplBase::partitionHostsPerLocality(const HostsPerLocality& hosts) {
  auto filtered_clones =
      hosts.filter({[](const Host& host) { return host.coarseHealth() == Host::Health::Healthy; },
                    [](const Host& host) { return host.coarseHealth() == Host::Health::Degraded; },
                    [](const Host& host) { return excludeBasedOnHealthFlag(host); }});

  return std::make_tuple(std::move(filtered_clones[0]), std::move(filtered_clones[1]),
                         std::move(filtered_clones[2]));
}

bool ClusterInfoImpl::maintenanceMode() const {
  return runtime_.snapshot().featureEnabled(maintenance_mode_runtime_key_, 0);
}

ResourceManager& ClusterInfoImpl::resourceManager(ResourcePriority priority) const {
  ASSERT(enumToInt(priority) < resource_managers_.managers_.size());
  return *resource_managers_.managers_[enumToInt(priority)];
}

void ClusterImplBase::initialize(std::function<void()> callback) {
  ASSERT(!initialization_started_);
  ASSERT(initialization_complete_callback_ == nullptr);
  initialization_complete_callback_ = callback;
  startPreInit();
}

void ClusterImplBase::onPreInitComplete() {
  // Protect against multiple calls.
  if (initialization_started_) {
    return;
  }
  initialization_started_ = true;

  ENVOY_LOG(debug, "initializing {} cluster {} completed",
            initializePhase() == InitializePhase::Primary ? "Primary" : "Secondary",
            info()->name());
  init_manager_.initialize(init_watcher_);
}

void ClusterImplBase::onInitDone() {
  if (health_checker_ && pending_initialize_health_checks_ == 0) {
    for (auto& host_set : prioritySet().hostSetsPerPriority()) {
      for (auto& host : host_set->hosts()) {
        if (host->disableActiveHealthCheck()) {
          continue;
        }
        ++pending_initialize_health_checks_;
      }
    }
    ENVOY_LOG(debug, "Cluster onInitDone pending initialize health check count {}",
              pending_initialize_health_checks_);

    // TODO(mattklein123): Remove this callback when done.
    health_checker_->addHostCheckCompleteCb([this](HostSharedPtr, HealthTransition) -> void {
      if (pending_initialize_health_checks_ > 0 && --pending_initialize_health_checks_ == 0) {
        finishInitialization();
      }
    });
  }

  if (pending_initialize_health_checks_ == 0) {
    finishInitialization();
  }
}

void ClusterImplBase::finishInitialization() {
  ASSERT(initialization_complete_callback_ != nullptr);
  ASSERT(initialization_started_);

  // Snap a copy of the completion callback so that we can set it to nullptr to unblock
  // reloadHealthyHosts(). See that function for more info on why we do this.
  auto snapped_callback = initialization_complete_callback_;
  initialization_complete_callback_ = nullptr;

  if (health_checker_ != nullptr) {
    reloadHealthyHosts(nullptr);
  }

  if (snapped_callback != nullptr) {
    snapped_callback();
  }
}

void ClusterImplBase::setHealthChecker(const HealthCheckerSharedPtr& health_checker) {
  ASSERT(!health_checker_);
  health_checker_ = health_checker;
  health_checker_->start();
  health_checker_->addHostCheckCompleteCb(
      [this](const HostSharedPtr& host, HealthTransition changed_state) -> void {
        // If we get a health check completion that resulted in a state change, signal to
        // update the host sets on all threads.
        if (changed_state == HealthTransition::Changed) {
          reloadHealthyHosts(host);
        }
      });
}

void ClusterImplBase::setOutlierDetector(const Outlier::DetectorSharedPtr& outlier_detector) {
  if (!outlier_detector) {
    return;
  }

  outlier_detector_ = outlier_detector;
  outlier_detector_->addChangedStateCb(
      [this](const HostSharedPtr& host) -> void { reloadHealthyHosts(host); });
}

void ClusterImplBase::setTransportFactoryContext(
    Server::Configuration::TransportSocketFactoryContextPtr transport_factory_context) {
  transport_factory_context_ = std::move(transport_factory_context);
}

void ClusterImplBase::reloadHealthyHosts(const HostSharedPtr& host) {
  // Every time a host changes Health Check state we cause a full healthy host recalculation which
  // for expensive LBs (ring, subset, etc.) can be quite time consuming. During startup, this
  // can also block worker threads by doing this repeatedly. There is no reason to do this
  // as we will not start taking traffic until we are initialized. By blocking Health Check
  // updates while initializing we can avoid this.
  if (initialization_complete_callback_ != nullptr) {
    return;
  }

  reloadHealthyHostsHelper(host);
}

void ClusterImplBase::reloadHealthyHostsHelper(const HostSharedPtr&) {
  const auto& host_sets = prioritySet().hostSetsPerPriority();
  for (size_t priority = 0; priority < host_sets.size(); ++priority) {
    const auto& host_set = host_sets[priority];
    // TODO(htuch): Can we skip these copies by exporting out const shared_ptr from HostSet?
    HostVectorConstSharedPtr hosts_copy = std::make_shared<HostVector>(host_set->hosts());

    HostsPerLocalityConstSharedPtr hosts_per_locality_copy = host_set->hostsPerLocality().clone();
    prioritySet().updateHosts(priority,
                              HostSetImpl::partitionHosts(hosts_copy, hosts_per_locality_copy),
                              host_set->localityWeights(), {}, {}, absl::nullopt);
  }
}

const Network::Address::InstanceConstSharedPtr
ClusterImplBase::resolveProtoAddress(const envoy::config::core::v3::Address& address) {
  TRY_ASSERT_MAIN_THREAD { return Network::Address::resolveProtoAddress(address); }
  END_TRY
  catch (EnvoyException& e) {
    if (info_->type() == envoy::config::cluster::v3::Cluster::STATIC ||
        info_->type() == envoy::config::cluster::v3::Cluster::EDS) {
      throw EnvoyException(fmt::format("{}. Consider setting resolver_name or setting cluster type "
                                       "to 'STRICT_DNS' or 'LOGICAL_DNS'",
                                       e.what()));
    }
    throw e;
  }
}

void ClusterImplBase::validateEndpointsForZoneAwareRouting(
    const envoy::config::endpoint::v3::LocalityLbEndpoints& endpoints) const {
  if (local_cluster_ && endpoints.priority() > 0) {
    throw EnvoyException(
        fmt::format("Unexpected non-zero priority for local cluster '{}'.", info()->name()));
  }
}

ClusterInfoImpl::OptionalClusterStats::OptionalClusterStats(
    const envoy::config::cluster::v3::Cluster& config, Stats::Scope& stats_scope,
    const ClusterManager& manager)
    : timeout_budget_stats_(
          (config.track_cluster_stats().timeout_budgets() || config.track_timeout_budgets())
              ? std::make_unique<ClusterTimeoutBudgetStats>(generateTimeoutBudgetStats(
                    stats_scope, manager.clusterTimeoutBudgetStatNames()))
              : nullptr),
      request_response_size_stats_(
          (config.track_cluster_stats().request_response_sizes()
               ? std::make_unique<ClusterRequestResponseSizeStats>(generateRequestResponseSizeStats(
                     stats_scope, manager.clusterRequestResponseSizeStatNames()))
               : nullptr)) {}

ClusterInfoImpl::ResourceManagers::ResourceManagers(
    const envoy::config::cluster::v3::Cluster& config, Runtime::Loader& runtime,
    const std::string& cluster_name, Stats::Scope& stats_scope,
    const ClusterCircuitBreakersStatNames& circuit_breakers_stat_names)
    : circuit_breakers_stat_names_(circuit_breakers_stat_names) {
  managers_[enumToInt(ResourcePriority::Default)] =
      load(config, runtime, cluster_name, stats_scope, envoy::config::core::v3::DEFAULT);
  managers_[enumToInt(ResourcePriority::High)] =
      load(config, runtime, cluster_name, stats_scope, envoy::config::core::v3::HIGH);
}

ClusterCircuitBreakersStats
ClusterInfoImpl::generateCircuitBreakersStats(Stats::Scope& scope, Stats::StatName prefix,
                                              bool track_remaining,
                                              const ClusterCircuitBreakersStatNames& stat_names) {
  auto make_gauge = [&stat_names, &scope, prefix](Stats::StatName stat_name) -> Stats::Gauge& {
    return Stats::Utility::gaugeFromElements(scope,
                                             {stat_names.circuit_breakers_, prefix, stat_name},
                                             Stats::Gauge::ImportMode::Accumulate);
  };

#define REMAINING_GAUGE(stat_name)                                                                 \
  track_remaining ? make_gauge(stat_name) : scope.store().nullGauge()

  return {
      make_gauge(stat_names.cx_open_),
      make_gauge(stat_names.cx_pool_open_),
      make_gauge(stat_names.rq_open_),
      make_gauge(stat_names.rq_pending_open_),
      make_gauge(stat_names.rq_retry_open_),
      REMAINING_GAUGE(stat_names.remaining_cx_),
      REMAINING_GAUGE(stat_names.remaining_cx_pools_),
      REMAINING_GAUGE(stat_names.remaining_pending_),
      REMAINING_GAUGE(stat_names.remaining_retries_),
      REMAINING_GAUGE(stat_names.remaining_rq_),
  };

#undef REMAINING_GAUGE
}

Http::Http1::CodecStats& ClusterInfoImpl::http1CodecStats() const {
  return Http::Http1::CodecStats::atomicGet(http1_codec_stats_, *stats_scope_);
}

Http::Http2::CodecStats& ClusterInfoImpl::http2CodecStats() const {
  return Http::Http2::CodecStats::atomicGet(http2_codec_stats_, *stats_scope_);
}

Http::Http3::CodecStats& ClusterInfoImpl::http3CodecStats() const {
  return Http::Http3::CodecStats::atomicGet(http3_codec_stats_, *stats_scope_);
}

#ifdef ENVOY_ENABLE_UHV
::Envoy::Http::HeaderValidatorStats&
ClusterInfoImpl::getHeaderValidatorStats(Http::Protocol protocol) const {
  switch (protocol) {
  case Http::Protocol::Http10:
  case Http::Protocol::Http11:
    return http1CodecStats();
  case Http::Protocol::Http2:
    return http2CodecStats();
  case Http::Protocol::Http3:
    return http3CodecStats();
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}
#endif

Http::HeaderValidatorPtr
ClusterInfoImpl::makeHeaderValidator([[maybe_unused]] Http::Protocol protocol) const {
#ifdef ENVOY_ENABLE_UHV
  return http_protocol_options_->header_validator_factory_
             ? http_protocol_options_->header_validator_factory_->create(
                   protocol, getHeaderValidatorStats(protocol))
             : nullptr;
#else
  return nullptr;
#endif
}

std::pair<absl::optional<double>, absl::optional<uint32_t>> ClusterInfoImpl::getRetryBudgetParams(
    const envoy::config::cluster::v3::CircuitBreakers::Thresholds& thresholds) {
  constexpr double default_budget_percent = 20.0;
  constexpr uint32_t default_retry_concurrency = 3;

  absl::optional<double> budget_percent;
  absl::optional<uint32_t> min_retry_concurrency;
  if (thresholds.has_retry_budget()) {
    // The budget_percent and min_retry_concurrency values are only set if there is a retry budget
    // message set in the cluster config.
    budget_percent = PROTOBUF_GET_WRAPPED_OR_DEFAULT(thresholds.retry_budget(), budget_percent,
                                                     default_budget_percent);
    min_retry_concurrency = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
        thresholds.retry_budget(), min_retry_concurrency, default_retry_concurrency);
  }
  return std::make_pair(budget_percent, min_retry_concurrency);
}

ResourceManagerImplPtr
ClusterInfoImpl::ResourceManagers::load(const envoy::config::cluster::v3::Cluster& config,
                                        Runtime::Loader& runtime, const std::string& cluster_name,
                                        Stats::Scope& stats_scope,
                                        const envoy::config::core::v3::RoutingPriority& priority) {
  uint64_t max_connections = 1024;
  uint64_t max_pending_requests = 1024;
  uint64_t max_requests = 1024;
  uint64_t max_retries = 3;
  uint64_t max_connection_pools = std::numeric_limits<uint64_t>::max();
  uint64_t max_connections_per_host = std::numeric_limits<uint64_t>::max();

  bool track_remaining = false;

  Stats::StatName priority_stat_name;
  std::string priority_name;
  switch (priority) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::core::v3::DEFAULT:
    priority_stat_name = circuit_breakers_stat_names_.default_;
    priority_name = "default";
    break;
  case envoy::config::core::v3::HIGH:
    priority_stat_name = circuit_breakers_stat_names_.high_;
    priority_name = "high";
    break;
  }

  const std::string runtime_prefix =
      fmt::format("circuit_breakers.{}.{}.", cluster_name, priority_name);

  const auto& thresholds = config.circuit_breakers().thresholds();
  const auto it = std::find_if(
      thresholds.cbegin(), thresholds.cend(),
      [priority](const envoy::config::cluster::v3::CircuitBreakers::Thresholds& threshold) {
        return threshold.priority() == priority;
      });
  const auto& per_host_thresholds = config.circuit_breakers().per_host_thresholds();
  const auto per_host_it = std::find_if(
      per_host_thresholds.cbegin(), per_host_thresholds.cend(),
      [priority](const envoy::config::cluster::v3::CircuitBreakers::Thresholds& threshold) {
        return threshold.priority() == priority;
      });

  absl::optional<double> budget_percent;
  absl::optional<uint32_t> min_retry_concurrency;
  if (it != thresholds.cend()) {
    max_connections = PROTOBUF_GET_WRAPPED_OR_DEFAULT(*it, max_connections, max_connections);
    max_pending_requests =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(*it, max_pending_requests, max_pending_requests);
    max_requests = PROTOBUF_GET_WRAPPED_OR_DEFAULT(*it, max_requests, max_requests);
    max_retries = PROTOBUF_GET_WRAPPED_OR_DEFAULT(*it, max_retries, max_retries);
    track_remaining = it->track_remaining();
    max_connection_pools =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(*it, max_connection_pools, max_connection_pools);
    std::tie(budget_percent, min_retry_concurrency) = ClusterInfoImpl::getRetryBudgetParams(*it);
  }
  if (per_host_it != per_host_thresholds.cend()) {
    if (per_host_it->has_max_pending_requests() || per_host_it->has_max_requests() ||
        per_host_it->has_max_retries() || per_host_it->has_max_connection_pools() ||
        per_host_it->has_retry_budget()) {
      throw EnvoyException("Unsupported field in per_host_thresholds");
    }
    if (per_host_it->has_max_connections()) {
      max_connections_per_host = per_host_it->max_connections().value();
    }
  }
  return std::make_unique<ResourceManagerImpl>(
      runtime, runtime_prefix, max_connections, max_pending_requests, max_requests, max_retries,
      max_connection_pools, max_connections_per_host,
      ClusterInfoImpl::generateCircuitBreakersStats(stats_scope, priority_stat_name,
                                                    track_remaining, circuit_breakers_stat_names_),
      budget_percent, min_retry_concurrency);
}

PriorityStateManager::PriorityStateManager(ClusterImplBase& cluster,
                                           const LocalInfo::LocalInfo& local_info,
                                           PrioritySet::HostUpdateCb* update_cb)
    : parent_(cluster), local_info_node_(local_info.node()), update_cb_(update_cb) {}

void PriorityStateManager::initializePriorityFor(
    const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint) {
  const uint32_t priority = locality_lb_endpoint.priority();
  if (priority_state_.size() <= priority) {
    priority_state_.resize(priority + 1);
  }
  if (priority_state_[priority].first == nullptr) {
    priority_state_[priority].first = std::make_unique<HostVector>();
  }
  if (locality_lb_endpoint.has_locality() && locality_lb_endpoint.has_load_balancing_weight()) {
    priority_state_[priority].second[locality_lb_endpoint.locality()] =
        locality_lb_endpoint.load_balancing_weight().value();
  }
}

void PriorityStateManager::registerHostForPriority(
    const std::string& hostname, Network::Address::InstanceConstSharedPtr address,
    const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint,
    const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint, TimeSource& time_source) {
  auto metadata = lb_endpoint.has_metadata()
                      ? parent_.constMetadataSharedPool()->getObject(lb_endpoint.metadata())
                      : nullptr;
  const auto host = std::make_shared<HostImpl>(
      parent_.info(), hostname, address, metadata, lb_endpoint.load_balancing_weight().value(),
      locality_lb_endpoint.locality(), lb_endpoint.endpoint().health_check_config(),
      locality_lb_endpoint.priority(), lb_endpoint.health_status(), time_source);
  registerHostForPriority(host, locality_lb_endpoint);
}

void PriorityStateManager::registerHostForPriority(
    const HostSharedPtr& host,
    const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint) {
  const uint32_t priority = locality_lb_endpoint.priority();
  // Should be called after initializePriorityFor.
  ASSERT(priority_state_[priority].first);
  priority_state_[priority].first->emplace_back(host);
}

void PriorityStateManager::updateClusterPrioritySet(
    const uint32_t priority, HostVectorSharedPtr&& current_hosts,
    const absl::optional<HostVector>& hosts_added, const absl::optional<HostVector>& hosts_removed,
    const absl::optional<Upstream::Host::HealthFlag> health_checker_flag,
    absl::optional<uint32_t> overprovisioning_factor) {
  // If local locality is not defined then skip populating per locality hosts.
  const auto& local_locality = local_info_node_.locality();
  ENVOY_LOG(trace, "Local locality: {}", local_locality.DebugString());

  // For non-EDS, most likely the current hosts are from priority_state_[priority].first.
  HostVectorSharedPtr hosts(std::move(current_hosts));
  LocalityWeightsMap empty_locality_map;
  LocalityWeightsMap& locality_weights_map =
      priority_state_.size() > priority ? priority_state_[priority].second : empty_locality_map;
  ASSERT(priority_state_.size() > priority || locality_weights_map.empty());
  LocalityWeightsSharedPtr locality_weights;
  std::vector<HostVector> per_locality;

  // If we are configured for locality weighted LB we populate the locality weights. We also
  // populate locality weights if the cluster uses load balancing extensions, since the extension
  // may want to make use of locality weights and we cannot tell by inspecting the config whether
  // this is the case.
  //
  // TODO: have the load balancing extension indicate, programmatically, whether it needs locality
  // weights, as an optimization in cases where it doesn't.
  const bool locality_weighted_lb =
      parent_.info()->lbConfig().has_locality_weighted_lb_config() ||
      parent_.info()->lbType() == LoadBalancerType::LoadBalancingPolicyConfig;
  if (locality_weighted_lb) {
    locality_weights = std::make_shared<LocalityWeights>();
  }

  // We use std::map to guarantee a stable ordering for zone aware routing.
  std::map<envoy::config::core::v3::Locality, HostVector, LocalityLess> hosts_per_locality;

  for (const HostSharedPtr& host : *hosts) {
    // Take into consideration when a non-EDS cluster has active health checking, i.e. to mark all
    // the hosts unhealthy (host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC)) and then fire
    // update callbacks to start the health checking process. The endpoint with disabled active
    // health check should not be set FAILED_ACTIVE_HC here.
    if (health_checker_flag.has_value() && !host->disableActiveHealthCheck()) {
      host->healthFlagSet(health_checker_flag.value());
    }
    hosts_per_locality[host->locality()].push_back(host);
  }

  // Do we have hosts for the local locality?
  const bool non_empty_local_locality =
      local_info_node_.has_locality() &&
      hosts_per_locality.find(local_locality) != hosts_per_locality.end();

  // As per HostsPerLocality::get(), the per_locality vector must have the local locality hosts
  // first if non_empty_local_locality.
  if (non_empty_local_locality) {
    per_locality.emplace_back(hosts_per_locality[local_locality]);
    if (locality_weighted_lb) {
      locality_weights->emplace_back(locality_weights_map[local_locality]);
    }
  }

  // After the local locality hosts (if any), we place the remaining locality host groups in
  // lexicographic order. This provides a stable ordering for zone aware routing.
  for (auto& entry : hosts_per_locality) {
    if (!non_empty_local_locality || !LocalityEqualTo()(local_locality, entry.first)) {
      per_locality.emplace_back(entry.second);
      if (locality_weighted_lb) {
        locality_weights->emplace_back(locality_weights_map[entry.first]);
      }
    }
  }

  auto per_locality_shared =
      std::make_shared<HostsPerLocalityImpl>(std::move(per_locality), non_empty_local_locality);

  // If a batch update callback was provided, use that. Otherwise directly update
  // the PrioritySet.
  if (update_cb_ != nullptr) {
    update_cb_->updateHosts(priority, HostSetImpl::partitionHosts(hosts, per_locality_shared),
                            std::move(locality_weights), hosts_added.value_or(*hosts),
                            hosts_removed.value_or<HostVector>({}), overprovisioning_factor);
  } else {
    parent_.prioritySet().updateHosts(
        priority, HostSetImpl::partitionHosts(hosts, per_locality_shared),
        std::move(locality_weights), hosts_added.value_or(*hosts),
        hosts_removed.value_or<HostVector>({}), overprovisioning_factor);
  }
}

bool BaseDynamicClusterImpl::updateDynamicHostList(
    const HostVector& new_hosts, HostVector& current_priority_hosts,
    HostVector& hosts_added_to_current_priority, HostVector& hosts_removed_from_current_priority,
    const HostMap& all_hosts, const absl::flat_hash_set<std::string>& all_new_hosts) {
  uint64_t max_host_weight = 1;

  // Did hosts change?
  //
  // Have host attributes changed the health of any endpoint? If so, we
  // rebuild the hosts vectors. We only do this if the health status of an
  // endpoint has materially changed (e.g. if previously failing active health
  // checks, we just note it's now failing EDS health status but don't rebuild).
  //
  // TODO(htuch): We can be smarter about this potentially, and not force a full
  // host set update on health status change. The way this would work is to
  // implement a HealthChecker subclass that provides thread local health
  // updates to the Cluster object. This will probably make sense to do in
  // conjunction with https://github.com/envoyproxy/envoy/issues/2874.
  bool hosts_changed = false;

  // Go through and see if the list we have is different from what we just got. If it is, we make
  // a new host list and raise a change notification. We also check for duplicates here. It's
  // possible for DNS to return the same address multiple times, and a bad EDS implementation
  // could do the same thing.

  // Keep track of hosts we see in new_hosts that we are able to match up with an existing host.
  absl::flat_hash_set<std::string> existing_hosts_for_current_priority(
      current_priority_hosts.size());
  // Keep track of hosts we're adding (or replacing)
  absl::flat_hash_set<std::string> new_hosts_for_current_priority(new_hosts.size());
  // Keep track of hosts for which locality is changed.
  absl::flat_hash_set<std::string> hosts_with_updated_locality_for_current_priority(
      current_priority_hosts.size());
  // Keep track of hosts for which active health check flag is changed.
  absl::flat_hash_set<std::string> hosts_with_active_health_check_flag_changed(
      current_priority_hosts.size());
  HostVector final_hosts;
  for (const HostSharedPtr& host : new_hosts) {
    // To match a new host with an existing host means comparing their addresses.
    auto existing_host = all_hosts.find(addressToString(host->address()));
    const bool existing_host_found = existing_host != all_hosts.end();

    // Clear any pending deletion flag on an existing host in case it came back while it was
    // being stabilized. We will set it again below if needed.
    if (existing_host_found) {
      existing_host->second->healthFlagClear(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL);
    }

    // Check if in-place host update should be skipped, i.e. when the following criteria are met
    // (currently there is only one criterion, but we might add more in the future):
    // - The cluster health checker is activated and a new host is matched with the existing one,
    //   but the health check address is different.
    const bool health_check_address_changed =
        (health_checker_ != nullptr && existing_host_found &&
         *existing_host->second->healthCheckAddress() != *host->healthCheckAddress());
    bool locality_changed = false;
    locality_changed = (existing_host_found &&
                        (!LocalityEqualTo()(host->locality(), existing_host->second->locality())));
    if (locality_changed) {
      hosts_with_updated_locality_for_current_priority.emplace(existing_host->first);
    }

    const bool active_health_check_flag_changed =
        (health_checker_ != nullptr && existing_host_found &&
         existing_host->second->disableActiveHealthCheck() != host->disableActiveHealthCheck());
    if (active_health_check_flag_changed) {
      hosts_with_active_health_check_flag_changed.emplace(existing_host->first);
    }
    const bool skip_inplace_host_update =
        health_check_address_changed || locality_changed || active_health_check_flag_changed;

    // When there is a match and we decided to do in-place update, we potentially update the
    // host's health check flag and metadata. Afterwards, the host is pushed back into the
    // final_hosts, i.e. hosts that should be preserved in the current priority.
    if (existing_host_found && !skip_inplace_host_update) {
      existing_hosts_for_current_priority.emplace(existing_host->first);
      // If we find a host matched based on address, we keep it. However we do change weight
      // inline so do that here.
      if (host->weight() > max_host_weight) {
        max_host_weight = host->weight();
      }
      if (existing_host->second->weight() != host->weight()) {
        existing_host->second->weight(host->weight());
        // We do full host set rebuilds so that load balancers can do pre-computation of data
        // structures based on host weight. This may become a performance problem in certain
        // deployments so it is runtime feature guarded and may also need to be configurable
        // and/or dynamic in the future.
        hosts_changed = true;
      }

      hosts_changed |=
          updateHealthFlag(*host, *existing_host->second, Host::HealthFlag::FAILED_EDS_HEALTH);
      hosts_changed |=
          updateHealthFlag(*host, *existing_host->second, Host::HealthFlag::DEGRADED_EDS_HEALTH);

      // Did metadata change?
      bool metadata_changed = true;
      if (host->metadata() && existing_host->second->metadata()) {
        metadata_changed = !Protobuf::util::MessageDifferencer::Equivalent(
            *host->metadata(), *existing_host->second->metadata());
      } else if (!host->metadata() && !existing_host->second->metadata()) {
        metadata_changed = false;
      }

      if (metadata_changed) {
        // First, update the entire metadata for the endpoint.
        existing_host->second->metadata(host->metadata());

        // Also, given that the canary attribute of an endpoint is derived from its metadata
        // (e.g.: from envoy.lb/canary), we do a blind update here since it's cheaper than testing
        // to see if it actually changed. We must update this besides just updating the metadata,
        // because it'll be used by the router filter to compute upstream stats.
        existing_host->second->canary(host->canary());

        // If metadata changed, we need to rebuild. See github issue #3810.
        hosts_changed = true;
      }

      // Did the priority change?
      if (host->priority() != existing_host->second->priority()) {
        existing_host->second->priority(host->priority());
        hosts_added_to_current_priority.emplace_back(existing_host->second);
      }

      final_hosts.push_back(existing_host->second);
    } else {
      new_hosts_for_current_priority.emplace(addressToString(host->address()));
      if (host->weight() > max_host_weight) {
        max_host_weight = host->weight();
      }

      // If we are depending on a health checker, we initialize to unhealthy.
      if (health_checker_ != nullptr && !host->disableActiveHealthCheck()) {
        host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);

        // If we want to exclude hosts until they have been health checked, mark them with
        // a flag to indicate that they have not been health checked yet.
        if (info_->warmHosts()) {
          host->healthFlagSet(Host::HealthFlag::PENDING_ACTIVE_HC);
        }
      }

      final_hosts.push_back(host);
      hosts_added_to_current_priority.push_back(host);
    }
  }

  // Remove hosts from current_priority_hosts that were matched to an existing host in the
  // previous loop.
  auto erase_from =
      std::remove_if(current_priority_hosts.begin(), current_priority_hosts.end(),
                     [&existing_hosts_for_current_priority](const HostSharedPtr& p) {
                       auto existing_itr =
                           existing_hosts_for_current_priority.find(p->address()->asString());

                       if (existing_itr != existing_hosts_for_current_priority.end()) {
                         existing_hosts_for_current_priority.erase(existing_itr);
                         return true;
                       }

                       return false;
                     });
  current_priority_hosts.erase(erase_from, current_priority_hosts.end());

  // If we saw existing hosts during this iteration from a different priority, then we've moved
  // a host from another priority into this one, so we should mark the priority as having changed.
  if (!existing_hosts_for_current_priority.empty()) {
    hosts_changed = true;
  }

  // The remaining hosts are hosts that are not referenced in the config update. We remove them
  // from the priority if any of the following is true:
  // - Active health checking is not enabled.
  // - The removed hosts are failing active health checking OR have been explicitly marked as
  //   unhealthy by a previous EDS update. We do not count outlier as a reason to remove a host
  //   or any other future health condition that may be added so we do not use the coarseHealth()
  //   API.
  // - We have explicitly configured the cluster to remove hosts regardless of active health
  // status.
  const bool dont_remove_healthy_hosts =
      health_checker_ != nullptr && !info()->drainConnectionsOnHostRemoval();
  if (!current_priority_hosts.empty() && dont_remove_healthy_hosts) {
    erase_from = std::remove_if(
        current_priority_hosts.begin(), current_priority_hosts.end(),
        [&all_new_hosts, &new_hosts_for_current_priority,
         &hosts_with_updated_locality_for_current_priority,
         &hosts_with_active_health_check_flag_changed, &final_hosts,
         &max_host_weight](const HostSharedPtr& p) {
          // This host has already been added as a new host in the
          // new_hosts_for_current_priority. Return false here to make sure that host
          // reference with older locality gets cleaned up from the priority.
          if (hosts_with_updated_locality_for_current_priority.contains(p->address()->asString())) {
            return false;
          }

          if (hosts_with_active_health_check_flag_changed.contains(p->address()->asString())) {
            return false;
          }

          if (all_new_hosts.contains(p->address()->asString()) &&
              !new_hosts_for_current_priority.contains(p->address()->asString())) {
            // If the address is being completely deleted from this priority, but is
            // referenced from another priority, then we assume that the other
            // priority will perform an in-place update to re-use the existing Host.
            // We should therefore not mark it as PENDING_DYNAMIC_REMOVAL, but
            // instead remove it immediately from this priority.
            // Example: health check address changed and priority also changed
            return false;
          }

          // PENDING_DYNAMIC_REMOVAL doesn't apply for the host with disabled active
          // health check, the host is removed immediately from this priority.
          if ((!(p->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC) ||
                 p->healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH))) &&
              !p->disableActiveHealthCheck()) {
            if (p->weight() > max_host_weight) {
              max_host_weight = p->weight();
            }

            final_hosts.push_back(p);
            p->healthFlagSet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL);
            return true;
          }
          return false;
        });
    current_priority_hosts.erase(erase_from, current_priority_hosts.end());
  }

  // At this point we've accounted for all the new hosts as well the hosts that previously
  // existed in this priority.
  info_->endpointStats().max_host_weight_.set(max_host_weight);

  // Whatever remains in current_priority_hosts should be removed.
  if (!hosts_added_to_current_priority.empty() || !current_priority_hosts.empty()) {
    hosts_removed_from_current_priority = std::move(current_priority_hosts);
    hosts_changed = true;
  }

  // During the update we populated final_hosts with all the hosts that should remain
  // in the current priority, so move them back into current_priority_hosts.
  current_priority_hosts = std::move(final_hosts);
  // We return false here in the absence of EDS health status or metadata changes, because we
  // have no changes to host vector status (modulo weights). When we have EDS
  // health status or metadata changed, we return true, causing updateHosts() to fire in the
  // caller.
  return hosts_changed;
}

Network::DnsLookupFamily
getDnsLookupFamilyFromCluster(const envoy::config::cluster::v3::Cluster& cluster) {
  return DnsUtils::getDnsLookupFamilyFromEnum(cluster.dns_lookup_family());
}

void reportUpstreamCxDestroy(const Upstream::HostDescriptionConstSharedPtr& host,
                             Network::ConnectionEvent event) {
  Upstream::ClusterTrafficStats& stats = *host->cluster().trafficStats();
  stats.upstream_cx_destroy_.inc();
  if (event == Network::ConnectionEvent::RemoteClose) {
    stats.upstream_cx_destroy_remote_.inc();
  } else {
    stats.upstream_cx_destroy_local_.inc();
  }
}

void reportUpstreamCxDestroyActiveRequest(const Upstream::HostDescriptionConstSharedPtr& host,
                                          Network::ConnectionEvent event) {
  Upstream::ClusterTrafficStats& stats = *host->cluster().trafficStats();
  stats.upstream_cx_destroy_with_active_rq_.inc();
  if (event == Network::ConnectionEvent::RemoteClose) {
    stats.upstream_cx_destroy_remote_with_active_rq_.inc();
  } else {
    stats.upstream_cx_destroy_local_with_active_rq_.inc();
  }
}

Network::Address::InstanceConstSharedPtr resolveHealthCheckAddress(
    const envoy::config::endpoint::v3::Endpoint::HealthCheckConfig& health_check_config,
    Network::Address::InstanceConstSharedPtr host_address) {
  Network::Address::InstanceConstSharedPtr health_check_address;
  const auto& port_value = health_check_config.port_value();
  if (health_check_config.has_address()) {
    auto address = Network::Address::resolveProtoAddress(health_check_config.address());
    health_check_address =
        port_value == 0 ? address : Network::Utility::getAddressWithPort(*address, port_value);
  } else {
    health_check_address = port_value == 0
                               ? host_address
                               : Network::Utility::getAddressWithPort(*host_address, port_value);
  }
  return health_check_address;
}

} // namespace Upstream
} // namespace Envoy
