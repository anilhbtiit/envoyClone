#include "cluster_manager_impl.h"
#include "health_checker_impl.h"
#include "load_balancer_impl.h"
#include "logical_dns_cluster.h"

#include "envoy/event/dispatcher.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"

#include "common/http/http1/conn_pool.h"
#include "common/http/http2/conn_pool.h"
#include "common/http/async_client_impl.h"

namespace Upstream {

ClusterManagerImpl::ClusterManagerImpl(const Json::Object& config, Stats::Store& stats,
                                       ThreadLocal::Instance& tls,
                                       Network::DnsResolver& dns_resolver,
                                       Ssl::ContextManager& ssl_context_manager,
                                       Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                                       const std::string& local_zone_name)
    : runtime_(runtime), tls_(tls), stats_(stats), thread_local_slot_(tls.allocateSlot()) {

  std::vector<Json::Object> clusters = config.getObjectArray("clusters");
  pending_cluster_init_ = clusters.size();

  if (config.hasObject("sds")) {
    pending_cluster_init_++;
    loadCluster(config.getObject("sds").getObject("cluster"), stats, dns_resolver,
                ssl_context_manager, runtime, random);

    SdsConfig sds_config{
        local_zone_name, config.getObject("sds").getObject("cluster").getString("name"),
        std::chrono::milliseconds(config.getObject("sds").getInteger("refresh_delay_ms"))};

    sds_config_.value(sds_config);
  }

  for (const Json::Object& cluster : clusters) {
    loadCluster(cluster, stats, dns_resolver, ssl_context_manager, runtime, random);
  }

  tls.set(thread_local_slot_,
          [this, &stats, &runtime, &random](Event::Dispatcher& dispatcher)
              -> ThreadLocal::ThreadLocalObjectPtr {
                return ThreadLocal::ThreadLocalObjectPtr{
                    new ThreadLocalClusterManagerImpl(*this, dispatcher, runtime, random)};
              });

  // To avoid threading issues, for those clusters that start with hosts already in them (like
  // the static cluster), we need to post an update onto each thread to notify them of the update.
  for (auto& cluster : primary_clusters_) {
    if (cluster.second->hosts().empty()) {
      continue;
    }

    postThreadLocalClusterUpdate(*cluster.second, cluster.second->hosts(), std::vector<HostPtr>{});
  }
}

void ClusterManagerImpl::loadCluster(const Json::Object& cluster, Stats::Store& stats,
                                     Network::DnsResolver& dns_resolver,
                                     Ssl::ContextManager& ssl_context_manager,
                                     Runtime::Loader& runtime, Runtime::RandomGenerator& random) {

  std::string string_type = cluster.getString("type");
  ClusterImplBasePtr new_cluster;
  if (string_type == "static") {
    new_cluster.reset(new StaticClusterImpl(cluster, stats, ssl_context_manager));
  } else if (string_type == "strict_dns") {
    new_cluster.reset(new StrictDnsClusterImpl(cluster, stats, ssl_context_manager, dns_resolver));
  } else if (string_type == "logical_dns") {
    new_cluster.reset(
        new LogicalDnsCluster(cluster, stats, ssl_context_manager, dns_resolver, tls_));
  } else if (string_type == "sds") {
    if (!sds_config_.valid()) {
      throw EnvoyException("cannot create an sds cluster without an sds config");
    }

    sds_clusters_.push_back(new SdsClusterImpl(cluster, stats, ssl_context_manager,
                                               sds_config_.value(), *this,
                                               dns_resolver.dispatcher(), random));
    new_cluster.reset(sds_clusters_.back());
  } else {
    throw EnvoyException(fmt::format("cluster: unknown cluster type '{}'", string_type));
  }

  if (primary_clusters_.find(new_cluster->name()) != primary_clusters_.end()) {
    throw EnvoyException(fmt::format("route: duplicate cluster '{}'", new_cluster->name()));
  }

  new_cluster->setInitializedCb([this]() -> void {
    ASSERT(pending_cluster_init_ > 0);
    if (--pending_cluster_init_ == 0) {
      if (initialized_callback_) {
        initialized_callback_();
      }
    } else if (pending_cluster_init_ == sds_clusters_.size()) {
      // All other clusters have initialized. Now we start up the SDS clusters since they will
      // depend on DNS resolution for the SDS cluster itself.
      for (SdsClusterImpl* cluster : sds_clusters_) {
        cluster->initialize();
      }
    }
  });

  const ClusterImplBase& primary_cluster_reference = *new_cluster;
  new_cluster->addMemberUpdateCb([&primary_cluster_reference, this](
      const std::vector<HostPtr>& hosts_added, const std::vector<HostPtr>& hosts_removed) {
    // This fires when a cluster is about to have an updated member set. We need to send this
    // out to all of the thread local configurations.
    postThreadLocalClusterUpdate(primary_cluster_reference, hosts_added, hosts_removed);
  });

  if (cluster.hasObject("health_check")) {
    Json::Object health_check_config = cluster.getObject("health_check");
    std::string hc_type = health_check_config.getString("type");
    if (hc_type == "http") {
      new_cluster->setHealthChecker(HealthCheckerPtr{new ProdHttpHealthCheckerImpl(
          *new_cluster, health_check_config, dns_resolver.dispatcher(), stats, runtime, random)});
    } else if (hc_type == "tcp") {
      new_cluster->setHealthChecker(HealthCheckerPtr{new TcpHealthCheckerImpl(
          *new_cluster, health_check_config, dns_resolver.dispatcher(), stats, runtime, random)});
    } else {
      throw EnvoyException(fmt::format("cluster: unknown health check type '{}'", hc_type));
    }
  }

  primary_clusters_.emplace(new_cluster->name(), new_cluster);
}

const Cluster* ClusterManagerImpl::get(const std::string& cluster) {
  auto entry = primary_clusters_.find(cluster);
  if (entry != primary_clusters_.end()) {
    return entry->second.get();
  } else {
    return nullptr;
  }
}

Http::ConnectionPool::Instance*
ClusterManagerImpl::httpConnPoolForCluster(const std::string& cluster) {
  ThreadLocalClusterManagerImpl& cluster_manager =
      tls_.getTyped<ThreadLocalClusterManagerImpl>(thread_local_slot_);

  // Select a host and create a connection pool for it if it does not already exist.
  auto entry = cluster_manager.thread_local_clusters_.find(cluster);
  ConstHostPtr host = entry->second->lb_->chooseHost();
  if (!host) {
    entry->second->primary_cluster_.stats().upstream_cx_none_healthy_.inc();
    return nullptr;
  }

  if (cluster_manager.host_http_conn_pool_map_.find(host) ==
      cluster_manager.host_http_conn_pool_map_.end()) {
    cluster_manager.host_http_conn_pool_map_[host] =
        allocateConnPool(cluster_manager.dispatcher_, host, stats_);
  }

  return cluster_manager.host_http_conn_pool_map_[host].get();
}

void ClusterManagerImpl::postThreadLocalClusterUpdate(const ClusterImplBase& primary_cluster,
                                                      const std::vector<HostPtr>& hosts_added,
                                                      const std::vector<HostPtr>& hosts_removed) {
  const std::string& name = primary_cluster.name();
  ConstHostVectorPtr hosts_copy = primary_cluster.rawHosts();
  ConstHostVectorPtr healthy_hosts_copy = primary_cluster.rawHealthyHosts();
  ConstHostVectorPtr local_zone_hosts_copy = primary_cluster.rawLocalZoneHosts();
  ConstHostVectorPtr local_zone_healthy_hosts_copy = primary_cluster.rawLocalZoneHealthyHosts();
  ThreadLocal::Instance& tls = tls_;
  uint32_t thead_local_slot = thread_local_slot_;
  tls_.runOnAllThreads(
      [name, hosts_copy, healthy_hosts_copy, local_zone_hosts_copy, local_zone_healthy_hosts_copy,
       hosts_added, hosts_removed, &tls, thead_local_slot]() mutable -> void {
        ThreadLocalClusterManagerImpl::updateClusterMembership(
            name, hosts_copy, healthy_hosts_copy, local_zone_hosts_copy,
            local_zone_healthy_hosts_copy, hosts_added, hosts_removed, tls, thead_local_slot);
      });
}

Host::CreateConnectionData ClusterManagerImpl::tcpConnForCluster(const std::string& cluster) {
  ThreadLocalClusterManagerImpl& cluster_manager =
      tls_.getTyped<ThreadLocalClusterManagerImpl>(thread_local_slot_);

  auto entry = cluster_manager.thread_local_clusters_.find(cluster);
  ConstHostPtr logical_host = entry->second->lb_->chooseHost();
  if (logical_host) {
    return logical_host->createConnection(cluster_manager.dispatcher_);
  } else {
    entry->second->primary_cluster_.stats().upstream_cx_none_healthy_.inc();
    return {nullptr, nullptr};
  }
}

Http::AsyncClientPtr ClusterManagerImpl::httpAsyncClientForCluster(const std::string& cluster) {
  Http::ConnectionPool::Instance* conn_pool = httpConnPoolForCluster(cluster);
  ThreadLocalClusterManagerImpl& cluster_manager =
      tls_.getTyped<ThreadLocalClusterManagerImpl>(thread_local_slot_);
  if (conn_pool) {
    return Http::AsyncClientPtr{
        new Http::AsyncClientImpl(*conn_pool, cluster, stats_, cluster_manager.dispatcher_)};
  } else {
    return nullptr;
  }
}

ClusterManagerImpl::ThreadLocalClusterManagerImpl::ThreadLocalClusterManagerImpl(
    ClusterManagerImpl& parent, Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
    Runtime::RandomGenerator& random)
    : dispatcher_(dispatcher) {
  for (auto& cluster : parent.primary_clusters_) {
    thread_local_clusters_[cluster.first].reset(new ClusterEntry(*cluster.second, runtime, random));
  }

  for (auto& cluster : thread_local_clusters_) {
    cluster.second->host_set_->addMemberUpdateCb(
        [this](const std::vector<HostPtr>&, const std::vector<HostPtr>& hosts_removed) -> void {
          // We need to go through and purge any connection pools for hosts that got deleted.
          // Right now hosts are specific to clusters, so even if two hosts actually point
          // to the same address this will be safe.

          for (const HostPtr& old_host : hosts_removed) {
            // Set a drained callback on the connection pool. When it is fully drained, we will
            // destroy it.
            auto conn_pool = host_http_conn_pool_map_.find(old_host);
            if (conn_pool != host_http_conn_pool_map_.end()) {
              conn_pool->second->addDrainedCallback([this, old_host]() -> void {
                dispatcher_.deferredDelete(std::move(host_http_conn_pool_map_[old_host]));
                host_http_conn_pool_map_.erase(old_host);
              });
            }
          }
        });
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::updateClusterMembership(
    const std::string& name, ConstHostVectorPtr hosts, ConstHostVectorPtr healthy_hosts,
    ConstHostVectorPtr local_zone_hosts, ConstHostVectorPtr local_zone_healthy_hosts,
    const std::vector<HostPtr>& hosts_added, const std::vector<HostPtr>& hosts_removed,
    ThreadLocal::Instance& tls, uint32_t thead_local_slot) {

  ThreadLocalClusterManagerImpl& config =
      tls.getTyped<ThreadLocalClusterManagerImpl>(thead_local_slot);

  ASSERT(config.thread_local_clusters_.find(name) != config.thread_local_clusters_.end());
  config.thread_local_clusters_[name]->host_set_->updateHosts(
      hosts, healthy_hosts, local_zone_hosts, local_zone_healthy_hosts, hosts_added, hosts_removed);
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::shutdown() {
  host_http_conn_pool_map_.clear();
}

ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::ClusterEntry(
    const Cluster& parent, Runtime::Loader& runtime, Runtime::RandomGenerator& random)
    : host_set_(new HostSetImpl()), primary_cluster_(parent) {

  switch (parent.lbType()) {
  case LoadBalancerType::LeastRequest: {
    lb_.reset(new LeastRequestLoadBalancer(*host_set_, parent.stats(), runtime, random));
    break;
  }
  case LoadBalancerType::Random: {
    lb_.reset(new RandomLoadBalancer(*host_set_, parent.stats(), runtime, random));
    break;
  }
  case LoadBalancerType::RoundRobin: {
    lb_.reset(new RoundRobinLoadBalancer(*host_set_, parent.stats(), runtime));
    break;
  }
  }
}

Http::ConnectionPool::InstancePtr
ProdClusterManagerImpl::allocateConnPool(Event::Dispatcher& dispatcher, ConstHostPtr host,
                                         Stats::Store& store) {
  if ((host->cluster().features() & Cluster::Features::HTTP2) &&
      runtime_.snapshot().featureEnabled("upstream.use_http2", 100)) {
    return Http::ConnectionPool::InstancePtr{
        new Http::Http2::ProdConnPoolImpl(dispatcher, host, store)};
  } else {
    return Http::ConnectionPool::InstancePtr{
        new Http::Http1::ConnPoolImplProd(dispatcher, host, store)};
  }
}

} // Upstream
