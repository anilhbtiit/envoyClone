#include "redis_cluster.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/redis/v3/redis_cluster.pb.h"
#include "envoy/extensions/clusters/redis/v3/redis_cluster.pb.validate.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

namespace {
Extensions::NetworkFilters::Common::Redis::Client::DoNothingPoolCallbacks null_pool_callbacks;
} // namespace

RedisCluster::RedisCluster(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::redis::v3::RedisClusterConfig& redis_cluster,
    NetworkFilters::Common::Redis::Client::ClientFactory& redis_client_factory,
    Upstream::ClusterManager& cluster_manager, Runtime::Loader& runtime, Api::Api& api,
    Network::DnsResolverSharedPtr dns_resolver,
    Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
    Stats::ScopePtr&& stats_scope, bool added_via_api,
    ClusterSlotUpdateCallBackSharedPtr lb_factory)
    : Upstream::BaseDynamicClusterImpl(cluster, runtime, factory_context, std::move(stats_scope),
                                       added_via_api,
                                       factory_context.mainThreadDispatcher().timeSource()),
      cluster_manager_(cluster_manager),
      cluster_refresh_rate_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(redis_cluster, cluster_refresh_rate, 5000))),
      cluster_refresh_timeout_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(redis_cluster, cluster_refresh_timeout, 3000))),
      redirect_refresh_interval_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(redis_cluster, redirect_refresh_interval, 5000))),
      redirect_refresh_threshold_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(redis_cluster, redirect_refresh_threshold, 5)),
      failure_refresh_threshold_(redis_cluster.failure_refresh_threshold()),
      host_degraded_refresh_threshold_(redis_cluster.host_degraded_refresh_threshold()),
      dispatcher_(factory_context.mainThreadDispatcher()), dns_resolver_(std::move(dns_resolver)),
      dns_lookup_family_(Upstream::getDnsLookupFamilyFromCluster(cluster)),
      load_assignment_(cluster.load_assignment()), local_info_(factory_context.localInfo()),
      random_(api.randomGenerator()), redis_discovery_session_(*this, redis_client_factory),
      lb_factory_(std::move(lb_factory)),
      auth_username_(
          NetworkFilters::RedisProxy::ProtocolOptionsConfigImpl::authUsername(info(), api)),
      auth_password_(
          NetworkFilters::RedisProxy::ProtocolOptionsConfigImpl::authPassword(info(), api)),
      cluster_name_(cluster.name()),
      refresh_manager_(Common::Redis::getClusterRefreshManager(
          factory_context.singletonManager(), factory_context.mainThreadDispatcher(),
          factory_context.clusterManager(), factory_context.api().timeSource())),
      registration_handle_(refresh_manager_->registerCluster(
          cluster_name_, redirect_refresh_interval_, redirect_refresh_threshold_,
          failure_refresh_threshold_, host_degraded_refresh_threshold_, [&]() {
            redis_discovery_session_.resolve_timer_->enableTimer(std::chrono::milliseconds(0));
          })) {
  const auto& locality_lb_endpoints = load_assignment_.endpoints();
  for (const auto& locality_lb_endpoint : locality_lb_endpoints) {
    for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      const auto& host = lb_endpoint.endpoint().address();
      dns_discovery_resolve_targets_.emplace_back(new DnsDiscoveryResolveTarget(
          *this, host.socket_address().address(), host.socket_address().port_value()));
    }
  }
}

void RedisCluster::startPreInit() {
  for (const DnsDiscoveryResolveTargetPtr& target : dns_discovery_resolve_targets_) {
    target->startResolveDns();
  }
}

void RedisCluster::updateAllHosts(const Upstream::HostVector& hosts_added,
                                  const Upstream::HostVector& hosts_removed,
                                  uint32_t current_priority) {
  Upstream::PriorityStateManager priority_state_manager(*this, local_info_, nullptr);

  auto locality_lb_endpoint = localityLbEndpoint();
  priority_state_manager.initializePriorityFor(locality_lb_endpoint);
  for (const Upstream::HostSharedPtr& host : hosts_) {
    if (locality_lb_endpoint.priority() == current_priority) {
      priority_state_manager.registerHostForPriority(host, locality_lb_endpoint);
    }
  }

  priority_state_manager.updateClusterPrioritySet(
      current_priority, std::move(priority_state_manager.priorityState()[current_priority].first),
      hosts_added, hosts_removed, absl::nullopt);
}

void RedisCluster::onClusterSlotUpdate(ClusterSlotsPtr&& slots) {
  Upstream::HostVector new_hosts;
  absl::flat_hash_set<std::string> all_new_hosts;

  for (const ClusterSlot& slot : *slots) {
    if (all_new_hosts.count(slot.primary()->asString()) == 0) {
      new_hosts.emplace_back(new RedisHost(info(), "", slot.primary(), *this, true, time_source_));
      all_new_hosts.emplace(slot.primary()->asString());
    }
    for (auto const& replica : slot.replicas()) {
      if (all_new_hosts.count(replica.first) == 0) {
        new_hosts.emplace_back(
            new RedisHost(info(), "", replica.second, *this, false, time_source_));
        all_new_hosts.emplace(replica.first);
      }
    }
  }

  // Get the map of all the latest existing hosts, which is used to filter out the existing
  // hosts in the process of updating cluster memberships.
  Upstream::HostMapConstSharedPtr all_hosts = priority_set_.crossPriorityHostMap();
  ASSERT(all_hosts != nullptr);

  Upstream::HostVector hosts_added;
  Upstream::HostVector hosts_removed;
  const bool host_updated = updateDynamicHostList(new_hosts, hosts_, hosts_added, hosts_removed,
                                                  *all_hosts, all_new_hosts);

  // Create a map containing all the latest hosts to determine whether the slots are updated.
  Upstream::HostMap updated_hosts = *all_hosts;
  for (const auto& host : hosts_removed) {
    updated_hosts.erase(host->address()->asString());
  }
  for (const auto& host : hosts_added) {
    updated_hosts[host->address()->asString()] = host;
  }

  const bool slot_updated =
      lb_factory_ ? lb_factory_->onClusterSlotUpdate(std::move(slots), updated_hosts) : false;

  // If slot is updated, call updateAllHosts regardless of if there's new hosts to force
  // update of the thread local load balancers.
  if (host_updated || slot_updated) {
    ASSERT(std::all_of(hosts_.begin(), hosts_.end(), [&](const auto& host) {
      return host->priority() == localityLbEndpoint().priority();
    }));
    updateAllHosts(hosts_added, hosts_removed, localityLbEndpoint().priority());
  } else {
    info_->stats().update_no_rebuild_.inc();
  }

  // TODO(hyang): If there is an initialize callback, fire it now. Note that if the
  // cluster refers to multiple DNS names, this will return initialized after a single
  // DNS resolution completes. This is not perfect but is easier to code and it is unclear
  // if the extra complexity is needed so will start with this.
  onPreInitComplete();
}

void RedisCluster::reloadHealthyHostsHelper(const Upstream::HostSharedPtr& host) {
  if (lb_factory_) {
    lb_factory_->onHostHealthUpdate();
  }
  if (host && (host->health() == Upstream::Host::Health::Degraded ||
               host->health() == Upstream::Host::Health::Unhealthy)) {
    refresh_manager_->onHostDegraded(cluster_name_);
  }
  ClusterImplBase::reloadHealthyHostsHelper(host);
}

// DnsDiscoveryResolveTarget
RedisCluster::DnsDiscoveryResolveTarget::DnsDiscoveryResolveTarget(RedisCluster& parent,
                                                                   const std::string& dns_address,
                                                                   const uint32_t port)
    : parent_(parent), dns_address_(dns_address), port_(port) {}

RedisCluster::DnsDiscoveryResolveTarget::~DnsDiscoveryResolveTarget() {
  if (active_query_) {
    active_query_->cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned);
  }
  // Disable timer for mock tests.
  if (resolve_timer_) {
    resolve_timer_->disableTimer();
  }
}

void RedisCluster::DnsDiscoveryResolveTarget::startResolveDns() {
  ENVOY_LOG(trace, "starting async DNS resolution for {}", dns_address_);

  active_query_ = parent_.dns_resolver_->resolve(
      dns_address_, parent_.dns_lookup_family_,
      [this](Network::DnsResolver::ResolutionStatus status,
             std::list<Network::DnsResponse>&& response) -> void {
        active_query_ = nullptr;
        ENVOY_LOG(trace, "async DNS resolution complete for {}", dns_address_);
        if (status == Network::DnsResolver::ResolutionStatus::Failure || response.empty()) {
          if (status == Network::DnsResolver::ResolutionStatus::Failure) {
            parent_.info_->stats().update_failure_.inc();
          } else {
            parent_.info_->stats().update_empty_.inc();
          }

          if (!resolve_timer_) {
            resolve_timer_ =
                parent_.dispatcher_.createTimer([this]() -> void { startResolveDns(); });
          }
          // if the initial dns resolved to empty, we'll skip the redis discovery phase and
          // treat it as an empty cluster.
          parent_.onPreInitComplete();
          resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
        } else {
          // Once the DNS resolve the initial set of addresses, call startResolveRedis on
          // the RedisDiscoverySession. The RedisDiscoverySession will using the "cluster
          // slots" command for service discovery and slot allocation. All subsequent
          // discoveries are handled by RedisDiscoverySession and will not use DNS
          // resolution again.
          parent_.redis_discovery_session_.registerDiscoveryAddress(std::move(response), port_);
          parent_.redis_discovery_session_.startResolveRedis();
        }
      });
}

// RedisCluster
RedisCluster::RedisDiscoverySession::RedisDiscoverySession(
    Envoy::Extensions::Clusters::Redis::RedisCluster& parent,
    NetworkFilters::Common::Redis::Client::ClientFactory& client_factory)
    : parent_(parent), dispatcher_(parent.dispatcher_),
      resolve_timer_(parent.dispatcher_.createTimer([this]() -> void { startResolveRedis(); })),
      client_factory_(client_factory), buffer_timeout_(0),
      redis_command_stats_(
          NetworkFilters::Common::Redis::RedisCommandStats::createRedisCommandStats(
              parent_.info()->statsScope().symbolTable())) {}

// Convert the cluster slot IP/Port response to and address, return null if the response
// does not match the expected type.
Network::Address::InstanceConstSharedPtr
RedisCluster::RedisDiscoverySession::RedisDiscoverySession::processClusterByIP(
    const NetworkFilters::Common::Redis::RespValue& value) {

  const auto& array = value.asArray();
  try {
    return Network::Utility::parseInternetAddress(array[0].asString(), array[1].asInteger(), false);
  } catch (const EnvoyException& ex) {
    // Probably ElastiCache use case: hostname instead of IP
    return nullptr;
  }
}

RedisCluster::RedisDiscoverySession::~RedisDiscoverySession() {
  if (current_request_) {
    current_request_->cancel();
    current_request_ = nullptr;
  }
  // Disable timer for mock tests.
  if (resolve_timer_) {
    resolve_timer_->disableTimer();
  }

  while (!client_map_.empty()) {
    client_map_.begin()->second->client_->close();
  }
}

void RedisCluster::RedisDiscoveryClient::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    auto client_to_delete = parent_.client_map_.find(host_);
    ASSERT(client_to_delete != parent_.client_map_.end());
    parent_.dispatcher_.deferredDelete(std::move(client_to_delete->second->client_));
    parent_.client_map_.erase(client_to_delete);
  }
}

void RedisCluster::RedisDiscoverySession::registerDiscoveryAddress(
    std::list<Envoy::Network::DnsResponse>&& response, const uint32_t port) {
  // Since the address from DNS does not have port, we need to make a new address that has
  // port in it.
  for (const Network::DnsResponse& res : response) {
    ASSERT(res.address_ != nullptr);
    discovery_address_list_.push_back(Network::Utility::getAddressWithPort(*(res.address_), port));
  }
}

void RedisCluster::RedisDiscoverySession::startResolveRedis() {
  parent_.info_->stats().update_attempt_.inc();
  // If a resolution is currently in progress, skip it.
  if (current_request_) {
    return;
  }

  // If hosts is empty, we haven't received a successful result from the CLUSTER SLOTS call
  // yet. So, pick a random discovery address from dns and make a request.
  Upstream::HostSharedPtr host;
  if (parent_.hosts_.empty()) {
    const int rand_idx = parent_.random_.random() % discovery_address_list_.size();
    auto it = std::next(discovery_address_list_.begin(), rand_idx);
    host = Upstream::HostSharedPtr{
        new RedisHost(parent_.info(), "", *it, parent_, true, parent_.timeSource())};
  } else {
    const int rand_idx = parent_.random_.random() % parent_.hosts_.size();
    host = parent_.hosts_[rand_idx];
  }

  current_host_address_ = host->address()->asString();
  RedisDiscoveryClientPtr& client = client_map_[current_host_address_];
  if (!client) {
    client = std::make_unique<RedisDiscoveryClient>(*this);
    client->host_ = current_host_address_;
    client->client_ = client_factory_.create(host, dispatcher_, *this, redis_command_stats_,
                                             parent_.info()->statsScope(), parent_.auth_username_,
                                             parent_.auth_password_);
    client->client_->addConnectionCallbacks(*client);
  }

  current_request_ = client->client_->makeRequest(ClusterSlotsRequest::instance_, *this);
}

void RedisCluster::RedisDiscoverySession::updateDnsStats(
    Network::DnsResolver::ResolutionStatus status, bool empty_response) {
  if (status == Network::DnsResolver::ResolutionStatus::Failure || empty_response) {
    if (status == Network::DnsResolver::ResolutionStatus::Failure) {
      parent_.info_->stats().update_failure_.inc();
    } else {
      parent_.info_->stats().update_empty_.inc();
    }
  }
}

void RedisCluster::RedisDiscoverySession::resolveClusterHostnames(ClusterSlotsPtr&& slots) {
  // Iterate over all slots replicate and resolve all missing addresses one at a time
  for (ClusterSlot& slot : *slots) {
    // Resolve primary
    if (slot.primary() == nullptr) {
      ENVOY_LOG(trace, "starting async DNS resolution for primary slot address {}",
                slot.primary_hostname_);
      parent_.dns_resolver_->resolve(
          slot.primary_hostname_, parent_.dns_lookup_family_,
          [this, &slot, &slots](Network::DnsResolver::ResolutionStatus status,
                                std::list<Network::DnsResponse>&& response) -> void {
            ENVOY_LOG(trace, "async DNS resolution complete for {}", slot.primary_hostname_);
            updateDnsStats(status, response.empty());
            if (status != Network::DnsResolver::ResolutionStatus::Success) {
              // Failed
              ENVOY_LOG(debug, "Unable to resolve cluster slot primary address {}",
                        slot.primary_hostname_);
              resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
              return;
            }
            // Primary slot address resolved
            slot.setPrimary(Network::Utility::getAddressWithPort(*response.front().address_,
                                                                 slot.primary_port_));
            // Continue resolving slot's addresses until everything is resolved
            resolveClusterHostnames(std::move(slots));
          });
      // do one resolution at a time: once resolved, callback will invoke this function again
      return;
    }
    // Resolve all replicas of the slot, one replica at a time
    if (!slot.replicas_to_resolve_.empty()) {
      const auto replica = slot.replicas_to_resolve_.back();
      slot.replicas_to_resolve_.pop_back();
      ENVOY_LOG(trace, "starting async DNS resolution for replica address {}", replica.first);
      parent_.dns_resolver_->resolve(
          replica.first, parent_.dns_lookup_family_,
          [this, &slot, &slots, &replica](Network::DnsResolver::ResolutionStatus status,
                                          std::list<Network::DnsResponse>&& response) -> void {
            ENVOY_LOG(trace, "async DNS resolution complete for {}", replica.first);
            updateDnsStats(status, response.empty());
            if (status != Network::DnsResolver::ResolutionStatus::Success) {
              // Failed
              ENVOY_LOG(debug, "Unable to resolve cluster replica address {}", replica.first);
              resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
              return;
            }
            // Replica resolved
            slot.addReplica(
                Network::Utility::getAddressWithPort(*response.front().address_, replica.second));
            // Continue resolving slot's addresses until everything is resolved
            resolveClusterHostnames(std::move(slots));
          });
      // do one resolution at a time: once resolved, callback will invoke this function again
      return;
    }
  } // of for(clusters slots)

  // All slots addresses were represented by DNS hostname lookup.
  parent_.onClusterSlotUpdate(std::move(slots));
  resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
}

void RedisCluster::RedisDiscoverySession::onResponse(
    NetworkFilters::Common::Redis::RespValuePtr&& value) {
  current_request_ = nullptr;

  const uint32_t SlotRangeStart = 0;
  const uint32_t SlotRangeEnd = 1;
  const uint32_t SlotPrimary = 2;
  const uint32_t SlotReplicaStart = 3;

  // Do nothing if the cluster is empty.
  if (value->type() != NetworkFilters::Common::Redis::RespType::Array || value->asArray().empty()) {
    onUnexpectedResponse(value);
    return;
  }

  auto cluster_slots = std::make_unique<std::vector<ClusterSlot>>();

  // CLUSTER SLOTS represents nested array of redis instances, like this:
  //
  // 1) 1) (integer) 0                <-- start slot range
  //    2) (integer) 5460             <-- end slot range
  //
  //    3) 1) "127.0.0.1"                                   |
  //       2) (integer) 30001                               <-  master for slot as IP(HOST) / Port /
  //       ID 3) "09dbe9720cda62f7865eabc5fd8857c5d2678366"    |
  //
  //    4) 1) "127.0.0.1"                                   |
  //       2) (integer) 30004                               <- replicas in the same format as master
  //       3) "821d8ca00d7ccf931ed3ffc7e3db0599d2271abf"    |
  //
  // Loop through the cluster slot response and error checks for each field.
  bool address_resolve_required = false;
  for (const NetworkFilters::Common::Redis::RespValue& part : value->asArray()) {
    if (part.type() != NetworkFilters::Common::Redis::RespType::Array) {
      onUnexpectedResponse(value);
      return;
    }

    // Row 1-2: Slot ranges
    const std::vector<NetworkFilters::Common::Redis::RespValue>& slot_range = part.asArray();
    if (slot_range.size() < 3 ||
        slot_range[SlotRangeStart].type() !=
            NetworkFilters::Common::Redis::RespType::Integer || // Start slot range is an
                                                                // integer.
        slot_range[SlotRangeEnd].type() !=
            NetworkFilters::Common::Redis::RespType::Integer) { // End slot range is an
                                                                // integer.
      onUnexpectedResponse(value);
      return;
    }

    // Row 3: Primary slot address
    if (!validateCluster(slot_range[SlotPrimary])) {
      onUnexpectedResponse(value);
      return;
    }
    // Try to parse primary slot address as IP address
    // It may fail in AWS ElastiCache use case: it uses hostnames instead of IPs.
    // If this is the case - we'll come back later and try to resolve hostnames asynchronously.
    ClusterSlot slot(slot_range[SlotRangeStart].asInteger(), slot_range[SlotRangeEnd].asInteger(),
                     processClusterByIP(slot_range[SlotPrimary]));
    if (slot.primary() == nullptr) {
      // Primary address is hostname: save the name for further resolving
      const auto& array = slot_range[SlotPrimary].asArray();
      slot.primary_hostname_ = array[0].asString();
      slot.primary_port_ = array[1].asInteger();
    }

    // Row 4-N: Replica(s) addresses
    for (auto replica = std::next(slot_range.begin(), SlotReplicaStart);
         replica != slot_range.end(); ++replica) {
      if (!validateCluster(*replica)) {
        onUnexpectedResponse(value);
        return;
      }
      auto replica_address = processClusterByIP(*replica);
      if (replica_address) {
        slot.addReplica(std::move(replica_address));
      } else {
        // Possible AWS ElastiCache use case: hostname instead of IP
        const auto& array = replica->asArray();
        slot.addReplicaToResolve(array[0].asString(), array[1].asInteger());
      }
    }
    // If at least one (primary, replicas) address is hostname, schedule DNS resolving
    if (slot.primary() == nullptr || !slot.replicas_to_resolve_.empty()) {
      address_resolve_required = true;
    }
    cluster_slots->push_back(std::move(slot));
  }

  if (!address_resolve_required) {
    // All slots addresses were represented by IP/Port pairs.
    parent_.onClusterSlotUpdate(std::move(cluster_slots));
    resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
  } else {
    // Resolve hostnames, once completed run onClusterSlotUpdate() / enable timer
    resolveClusterHostnames(std::move(cluster_slots));
  }
}

// Ensure that Slot Cluster response has valid format
bool RedisCluster::RedisDiscoverySession::validateCluster(
    const NetworkFilters::Common::Redis::RespValue& value) {
  // Verify data types
  if (value.type() != NetworkFilters::Common::Redis::RespType::Array) {
    return false;
  }
  const auto& array = value.asArray();
  if (array.size() < 2 || array[0].type() != NetworkFilters::Common::Redis::RespType::BulkString ||
      array[1].type() != NetworkFilters::Common::Redis::RespType::Integer) {
    return false;
  }
  // Verify IP/Host address
  if (array[0].asString().empty()) {
    return false;
  }
  // Verify port
  if (array[1].asInteger() > 0xffff) {
    return false;
  }

  return true;
}

void RedisCluster::RedisDiscoverySession::onUnexpectedResponse(
    const NetworkFilters::Common::Redis::RespValuePtr& value) {
  ENVOY_LOG(warn, "Unexpected response to cluster slot command: {}", value->toString());
  this->parent_.info_->stats().update_failure_.inc();
  resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
}

void RedisCluster::RedisDiscoverySession::onFailure() {
  current_request_ = nullptr;
  if (!current_host_address_.empty()) {
    auto client_to_delete = client_map_.find(current_host_address_);
    client_to_delete->second->client_->close();
  }
  parent_.info()->stats().update_failure_.inc();
  resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
}

RedisCluster::ClusterSlotsRequest RedisCluster::ClusterSlotsRequest::instance_;

std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
RedisClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::redis::v3::RedisClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context,
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
    Envoy::Stats::ScopePtr&& stats_scope) {
  if (!cluster.has_cluster_type() || cluster.cluster_type().name() != "envoy.clusters.redis") {
    throw EnvoyException("Redis cluster can only created with redis cluster type.");
  }
  // TODO(hyang): This is needed to migrate existing cluster, disallow using other lb_policy
  // in the future
  if (cluster.lb_policy() != envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED) {
    return std::make_pair(std::make_shared<RedisCluster>(
                              cluster, proto_config,
                              NetworkFilters::Common::Redis::Client::ClientFactoryImpl::instance_,
                              context.clusterManager(), context.runtime(), context.api(),
                              selectDnsResolver(cluster, context), socket_factory_context,
                              std::move(stats_scope), context.addedViaApi(), nullptr),
                          nullptr);
  }
  auto lb_factory =
      std::make_shared<RedisClusterLoadBalancerFactory>(context.api().randomGenerator());
  return std::make_pair(std::make_shared<RedisCluster>(
                            cluster, proto_config,
                            NetworkFilters::Common::Redis::Client::ClientFactoryImpl::instance_,
                            context.clusterManager(), context.runtime(), context.api(),
                            selectDnsResolver(cluster, context), socket_factory_context,
                            std::move(stats_scope), context.addedViaApi(), lb_factory),
                        std::make_unique<RedisClusterThreadAwareLoadBalancer>(lb_factory));
}

REGISTER_FACTORY(RedisClusterFactory, Upstream::ClusterFactory);

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
