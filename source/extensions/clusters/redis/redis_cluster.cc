#include "redis_cluster.h"

//#include "common/http/utility.h"
//#include "common/network/resolver_impl.h"
//#include "common/network/socket_option_factory.h"
//#include "common/upstream/eds.h"
//#include "common/upstream/health_checker_impl.h"
//#include "common/upstream/logical_dns_cluster.h"
//#include "common/upstream/original_dst_cluster.h"


namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

RedisCluster::RedisCluster(const envoy::api::v2::Cluster &cluster,
                           const envoy::config::cluster::redis::RedisClusterConfig &redisCluster,
                           NetworkFilters::Common::Redis::Client::ClientFactory &redis_client_factory,
                           Upstream::ClusterManager &clusterManager, Runtime::Loader &runtime,
                           Network::DnsResolverSharedPtr dns_resolver,
                           Server::Configuration::TransportSocketFactoryContext &factory_context,
                           Stats::ScopePtr &&stats_scope, bool added_via_api) :
  Upstream::BaseDynamicClusterImpl(cluster, runtime, factory_context, std::move(stats_scope), added_via_api),
  cluster_manager_(clusterManager),
  cluster_refresh_rate_(
    std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(redisCluster, cluster_refresh_rate, 5000))),
  cluster_refresh_timeout_(
    std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(redisCluster, cluster_refresh_timeout, 3000))),
  dispatcher_(factory_context.dispatcher()), dns_resolver_(dns_resolver),
  load_assignment_(cluster.has_load_assignment() ? cluster.load_assignment()
                                                 : Config::Utility::translateClusterHosts(cluster.hosts())),
  local_info_(factory_context.localInfo()),
  random_(factory_context.random()),
  redis_discovery_session_(std::make_unique<RedisDiscoverySession>(*this, redis_client_factory)){
  switch (cluster.dns_lookup_family()) {
    case envoy::api::v2::Cluster::V6_ONLY:
      dns_lookup_family_ = Network::DnsLookupFamily::V6Only;
      break;
    case envoy::api::v2::Cluster::V4_ONLY:
      dns_lookup_family_ = Network::DnsLookupFamily::V4Only;
      break;
    case envoy::api::v2::Cluster::AUTO:
      dns_lookup_family_ = Network::DnsLookupFamily::Auto;
      break;
    default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  const auto &locality_lb_endpoints = load_assignment_.endpoints();
  for (const auto &locality_lb_endpoint : locality_lb_endpoints) {
    for (const auto &lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      const auto &host = lb_endpoint.endpoint().address();
      discovery_resolve_targets_.emplace_back(new DiscoveryResolveTarget(*this, host.socket_address().address(),
                                                                         locality_lb_endpoint, lb_endpoint));
    }
  }
};

void RedisCluster::startPreInit() {
  for (const DiscoveryResolveTargetPtr &target : discovery_resolve_targets_) {
    target->startResolve();
  }
}

void RedisCluster::updateAllHosts(const Upstream::HostVector& hosts_added, const Upstream::HostVector& hosts_removed,
  uint32_t current_priority) {
  Upstream::PriorityStateManager priority_state_manager(*this, local_info_, nullptr);

  priority_state_manager.initializePriorityFor(redis_discovery_session_->locality_lb_endpoint_);
  for (const Upstream::HostSharedPtr& host : redis_discovery_session_->hosts_ ) {
    if (redis_discovery_session_->locality_lb_endpoint_.priority() == current_priority) {
      priority_state_manager.registerHostForPriority(host, redis_discovery_session_->locality_lb_endpoint_);
    }
  }

  priority_state_manager.updateClusterPrioritySet(
    current_priority, std::move(priority_state_manager.priorityState()[current_priority].first),
    hosts_added, hosts_removed, absl::nullopt);
}

// DiscoveryResolveTarget
RedisCluster::DiscoveryResolveTarget::DiscoveryResolveTarget(
  RedisCluster & parent,
  const std::string &dns_address,
  const envoy::api::v2::endpoint::LocalityLbEndpoints &locality_lb_endpoint,
  const envoy::api::v2::endpoint::LbEndpoint &lb_endpoint
  ) : parent_ (parent), dns_address_(dns_address),
  locality_lb_endpoint_(locality_lb_endpoint), lb_endpoint_(lb_endpoint) {}

RedisCluster::DiscoveryResolveTarget::~DiscoveryResolveTarget() {
  if (active_query_) {
    active_query_->cancel();
  }
}

void RedisCluster::DiscoveryResolveTarget::startResolve() {
  ENVOY_LOG(trace, "starting async DNS resolution for {}", dns_address_);
  parent_.info_->stats().update_attempt_.inc();

  active_query_ = parent_.dns_resolver_->resolve(
    dns_address_, parent_.dns_lookup_family_,
    [this](const std::list<Network::Address::InstanceConstSharedPtr> &&address_list) -> void {
      active_query_ = nullptr;
      ENVOY_LOG(trace, "async DNS resolution complete for {}", dns_address_);
      parent_.info_->stats().update_success_.inc();
      const int rand_idx = parent_.random_.random() % address_list.size();
      auto it = address_list.begin();
      std::next(it, rand_idx);
      Upstream::HostSharedPtr host_ptr {new RedisHost(parent_.info(), "", *it, parent_, true)};
      parent_.redis_discovery_session_->startResolveWithHost(std::move(host_ptr));
    });
}

//RedisCluster
RedisCluster::RedisDiscoverySession::RedisDiscoverySession(Envoy::Extensions::Clusters::Redis::RedisCluster &parent, NetworkFilters::Common::Redis::Client::ClientFactory& client_factory) :
  parent_(parent),
  resolve_timer_(parent.dispatcher_.createTimer([this]() -> void { startResolveWithRandomHost(); })),
  client_factory_(client_factory){ }

inline Network::Address::InstanceConstSharedPtr
ProcessCluster(NetworkFilters::Common::Redis::RespValue& value) {
  ASSERT(value.type() == NetworkFilters::Common::Redis::RespType::Array);
  ASSERT(value.asArray().size() >= 2);
  return std::make_shared<Network::Address::Ipv4Instance>(value.asArray()[0].asString(), value.asArray()[1].asInteger());
}

RedisCluster::RedisDiscoverySession::~RedisDiscoverySession() {
  if (current_request_) {
    current_request_->cancel();
    current_request_ = nullptr;
  }

  if (client_) {
    client_->close();
  }
}

void RedisCluster::RedisDiscoverySession::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    // This should only happen after any active requests have been failed/cancelled.
    ASSERT(!current_request_);
    parent_.dispatcher_.deferredDelete(std::move(client_));
  }
}

void RedisCluster::RedisDiscoverySession::startResolveWithRandomHost() {

  const int rand_idx = parent_.random_.random() % hosts_.size();
  auto host = hosts_[rand_idx];
  startResolveWithHost(std::move(host));
}

void RedisCluster::RedisDiscoverySession::startResolveWithHost(const Upstream::HostSharedPtr &host) {
  // if a resolution is current in progress, skip
  if (current_request_) {
    return;
  }

  if (!client_) {
    client_ = client_factory_.create(host, parent_.dispatcher_, *this);
    client_->addConnectionCallbacks(*this);
  }

  ClusterSlotsRequest request;
  client_->makeRequest(request, *this);
}

void RedisCluster::RedisDiscoverySession::onResponse(
  NetworkFilters::Common::Redis::RespValuePtr &&value) {

  current_request_ = nullptr;
  //TODO(hyang): move to use connection pool for the cluster and avoid closing the connection each time
  client_->close();

  if (value->type() != NetworkFilters::Common::Redis::RespType::Array) {
    ENVOY_LOG(error, "Unexpected response to cluster slot command: {}", value->toString());
    parent_.info_->stats().update_no_rebuild_.inc();
    return;
  }

  Upstream::HostVector new_hosts;
  std::array<std::string, 16384> slots_;

  for (auto it = begin(value->asArray()); it != end(value->asArray()); ++it) {
    // each slot range is an array
    auto &slotRange = it->asArray();
    ASSERT(slotRange.size() >= 3);

    // field 0: start slot range
    auto startField = slotRange[0].asInteger();

    // field 1: end slot range
    auto endField = slotRange[1].asInteger();

    // field 2: master address for slot range
    // TODO(hyang): for now we're only adding the master node for each slot, when we're ready to
    //  send requests to slave nodes, we need to add subsequent address in the response as slave nodes
    auto masterAddress = ProcessCluster(slotRange[2]);
    // add to the slot
    for(auto i = startField; i <= endField; ++i) {
      slots_[i] = masterAddress->asString();
    }
    new_hosts.emplace_back(new RedisHost(parent_.info(), "", std::move(masterAddress), parent_, true));
  }

  std::unordered_map<std::string, Upstream::HostSharedPtr> updated_hosts;
  Upstream::HostVector hosts_added;
  Upstream::HostVector hosts_removed;
  if (parent_.updateDynamicHostList(new_hosts, hosts_, hosts_added, hosts_removed,
                                    updated_hosts, all_hosts_)) {
    ASSERT(std::all_of(hosts_.begin(), hosts_.end(), [&](const auto& host) {
      return host->priority() == locality_lb_endpoint_.priority();
    }));
    parent_.updateAllHosts(hosts_added, hosts_removed, locality_lb_endpoint_.priority());
  } else {
    parent_.info_->stats().update_no_rebuild_.inc();
  }

  all_hosts_ = std::move(updated_hosts);
  cluster_slots_map_.swap(slots_);

  // If there is an initialize callback, fire it now. Note that if the cluster refers to
  // multiple DNS names, this will return initialized after a single DNS resolution
  // completes. This is not perfect but is easier to code and unclear if the extra
  // complexity is needed so will start with this.
  parent_.onPreInitComplete();
  resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
}

void RedisCluster::RedisDiscoverySession::onFailure() {
  client_->close();
  current_request_ = nullptr;
  parent_.info()->stats().update_failure_.inc();
  resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
}

Upstream::ClusterImplBaseSharedPtr RedisClusterFactory::createClusterWithConfig(
  const envoy::api::v2::Cluster &cluster,
  const envoy::config::cluster::redis::RedisClusterConfig &proto_config,
  Upstream::ClusterFactoryContext &context,
  Envoy::Server::Configuration::TransportSocketFactoryContext &socket_factory_context,
  Envoy::Stats::ScopePtr &&stats_scope) {
  if (!cluster.has_cluster_type() || cluster.cluster_type().name() != Extensions::Clusters::ClusterTypes::get().Redis) {
    throw EnvoyException("Redis cluster can only created with redis cluster type");
  }
  return std::make_shared<RedisCluster>(cluster, proto_config,
    NetworkFilters::Common::Redis::Client::ClientFactoryImpl::instance_, context.clusterManager(), context.runtime(),
    selectDnsResolver(cluster, context), socket_factory_context, std::move(stats_scope), context.addedViaApi());
}

REGISTER_FACTORY(RedisClusterFactory, Upstream::ClusterFactory);

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
