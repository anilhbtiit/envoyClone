#include "extensions/upstreams/tcp/factories/default_tcp_upstream_factory.h"

#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/cluster_manager.h"

#include "extensions/upstreams/tcp/http/upstream_request.h"
#include "extensions/upstreams/tcp/tcp/upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Tcp {

/**
 * Config registration for the original dst filter. @see NamedNetworkFilterConfigFactory.
 */
Envoy::Tcp::ConnectionHandlePtr DefaultTcpUpstreamFactory::createTcpUpstreamHandle(
    Envoy::Upstream::ClusterManager& cluster_manager,
    Envoy::Upstream::LoadBalancerContext* lb_context,
    Envoy::Tcp::GenericUpstreamPoolCallbacks& generic_pool_callbacks,
    const std::shared_ptr<Envoy::Tcp::ConnectionPool::UpstreamCallbacks>& upstream_callbacks,
    absl::string_view hostname, const std::string& cluster_name) {

  if (hostname.empty()) {
    Envoy::Tcp::ConnectionPool::Instance* conn_pool = cluster_manager.tcpConnPoolForCluster(
        cluster_name, Envoy::Upstream::ResourcePriority::Default, lb_context);
    if (conn_pool) {
      auto tcp_handle =
          std::make_unique<Envoy::Extensions::Upstreams::Tcp::Tcp::TcpConnectionHandle>(
              nullptr, *upstream_callbacks, generic_pool_callbacks);
      Envoy::Tcp::ConnectionPool::Cancellable* cancellable = conn_pool->newConnection(*tcp_handle);
      tcp_handle->setUpstreamHandle(cancellable);
      return tcp_handle;
    }
  } else {
    auto* cluster = cluster_manager.get(cluster_name);
    if (!cluster) {
      return nullptr;
    }
    // TODO(snowp): Ideally we should prevent this from being configured, but that's tricky to get
    // right since whether a cluster is invalid depends on both the tcp_proxy config + cluster
    // config.
    if ((cluster->info()->features() & Upstream::ClusterInfo::Features::HTTP2) == 0) {
      ENVOY_LOG_MISC(
          error,
          "Attempted to tunnel over HTTP/1.1 from cluster {}, this is not supported. Set "
          "http2_protocol_options on the cluster.",
          cluster_name);
      return nullptr;
    }
    Envoy::Http::ConnectionPool::Instance* conn_pool = cluster_manager.httpConnPoolForCluster(
        cluster_name, Envoy::Upstream::ResourcePriority::Default, absl::nullopt, lb_context);
    if (conn_pool) {
      auto http_handle =
          std::make_unique<Envoy::Extensions::Upstreams::Tcp::Http::HttpConnectionHandle>(
              nullptr, generic_pool_callbacks);
      auto http_upstream = std::make_shared<Envoy::Extensions::Upstreams::Tcp::Http::HttpUpstream>(
          *upstream_callbacks, std::string(hostname));
      // Always create new handle so that handle and http_upstream is 1:1 mapping.
      http_handle->setUpstream(http_upstream);
      Envoy::Http::ConnectionPool::Cancellable* cancellable =
          conn_pool->newStream(http_upstream->responseDecoder(), *http_handle);
      http_handle->setUpstreamHandle(cancellable);
      return http_handle;
    }
  }
  return nullptr;
}
} // namespace Tcp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy