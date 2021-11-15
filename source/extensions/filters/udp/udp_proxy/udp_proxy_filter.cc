#include "source/extensions/filters/udp/udp_proxy/udp_proxy_filter.h"

#include "envoy/network/listener.h"

#include "source/common/network/socket_option_factory.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

UdpProxyFilter::UdpProxyFilter(Network::UdpReadFilterCallbacks& callbacks,
                               const UdpProxyFilterConfigSharedPtr& config)
    : UdpListenerReadFilter(callbacks), config_(config),
      cluster_update_callbacks_(
          config->clusterManager().addThreadLocalClusterUpdateCallbacks(*this)) {
  Upstream::ThreadLocalCluster* cluster =
      config->clusterManager().getThreadLocalCluster(config->cluster());
  if (cluster != nullptr) {
    onClusterAddOrUpdate(*cluster);
  }
}

void UdpProxyFilter::onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) {
  if (cluster.info()->name() != config_->cluster()) {
    return;
  }

  ENVOY_LOG(debug, "udp proxy: attaching to cluster {}", cluster.info()->name());
  ASSERT(cluster_info_ == absl::nullopt || &cluster_info_.value()->cluster_ != &cluster);

  if (config_->usingPerPacketLoadBalancing()) {
    cluster_info_.emplace(std::make_unique<PerPacketLoadBalancingClusterInfo>(*this, cluster));
  } else {
    cluster_info_.emplace(std::make_unique<StickySessionClusterInfo>(*this, cluster));
  }
}

void UdpProxyFilter::onClusterRemoval(const std::string& cluster) {
  if (cluster != config_->cluster()) {
    return;
  }

  ENVOY_LOG(debug, "udp proxy: detaching from cluster {}", cluster);
  cluster_info_.reset();
}

Network::FilterStatus UdpProxyFilter::onData(Network::UdpRecvData& data) {
  if (!cluster_info_.has_value()) {
    config_->stats().downstream_sess_no_route_.inc();
    return Network::FilterStatus::StopIteration;
  }

  return cluster_info_.value()->onData(data);
}

Network::FilterStatus UdpProxyFilter::onReceiveError(Api::IoError::IoErrorCode) {
  config_->stats().downstream_sess_rx_errors_.inc();

  return Network::FilterStatus::StopIteration;
}

UdpProxyFilter::ClusterInfo::ClusterInfo(UdpProxyFilter& filter,
                                         Upstream::ThreadLocalCluster& cluster)
    : filter_(filter), cluster_(cluster),
      cluster_stats_(generateStats(cluster.info()->statsScope())),
      member_update_cb_handle_(cluster.prioritySet().addMemberUpdateCb(
          [this](const Upstream::HostVector&, const Upstream::HostVector& hosts_removed) {
            for (const auto& host : hosts_removed) {
              removeHostSessions(host);
            }
          })) {}

UdpProxyFilter::ClusterInfo::~ClusterInfo() { ASSERT(host_to_sessions_.empty()); }

void UdpProxyFilter::ClusterInfo::removeSession(const ActiveSession* session) {
  // First remove from the host to sessions map.
  ASSERT(host_to_sessions_[&session->host()].count(session) == 1);
  auto host_sessions_it = host_to_sessions_.find(&session->host());
  host_sessions_it->second.erase(session);
  if (host_sessions_it->second.empty()) {
    host_to_sessions_.erase(host_sessions_it);
  }

  // Now remove it from the main storage.
  removeSessionFromStorage(session);
}

UdpProxyFilter::ActiveSession*
UdpProxyFilter::ClusterInfo::createSession(Network::UdpRecvData::LocalPeerAddresses&& addresses,
                                           const HostConstSharedPtrOptConstRef& optional_host) {
  if (!cluster_.info()
           ->resourceManager(Upstream::ResourcePriority::Default)
           .connections()
           .canCreate()) {
    ENVOY_LOG(debug, "cannot create new connection.");
    cluster_.info()->stats().upstream_cx_overflow_.inc();
    return nullptr;
  }

  if (optional_host) {
    return createSessionWithHost(std::move(addresses), *optional_host);
  }

  auto host = chooseHost(addresses.peer_);
  if (host == nullptr) {
    ENVOY_LOG(debug, "cannot find any valid host.");
    cluster_.info()->stats().upstream_cx_none_healthy_.inc();
    return nullptr;
  }
  return createSessionWithHost(std::move(addresses), host);
}

UdpProxyFilter::ActiveSession* UdpProxyFilter::ClusterInfo::createSessionWithHost(
    Network::UdpRecvData::LocalPeerAddresses&& addresses,
    const Upstream::HostConstSharedPtr& host) {
  ASSERT(host);
  auto new_session = std::make_unique<ActiveSession>(*this, std::move(addresses), host);
  auto new_session_ptr = new_session.get();
  storeSession(std::move(new_session));
  host_to_sessions_[host.get()].emplace(new_session_ptr);
  return new_session_ptr;
}

Upstream::HostConstSharedPtr UdpProxyFilter::ClusterInfo::chooseHost(
    const Network::Address::InstanceConstSharedPtr& peer_address) const {
  UdpLoadBalancerContext context(filter_.config_->hashPolicy(), peer_address);
  Upstream::HostConstSharedPtr host = cluster_.loadBalancer().chooseHost(&context);
  return host;
}

void UdpProxyFilter::ClusterInfo::removeHostSessions(const Upstream::HostConstSharedPtr& host) {
  auto host_sessions_it = host_to_sessions_.find(host.get());
  if (host_sessions_it != host_to_sessions_.end()) {
    for (const auto& session : host_sessions_it->second) {
      removeSessionFromStorage(session);
    }
    host_to_sessions_.erase(host_sessions_it);
  }
}

UdpProxyFilter::StickySessionClusterInfo::StickySessionClusterInfo(
    UdpProxyFilter& filter, Upstream::ThreadLocalCluster& cluster)
    : ClusterInfo(filter, cluster) {}

UdpProxyFilter::StickySessionClusterInfo::~StickySessionClusterInfo() {
  // Sanity check the session accounting. This is not as fast as a straight teardown, but this is
  // not a performance critical path.
  while (!sessions_.empty()) {
    removeSession(sessions_.begin()->get());
  }
}

Network::FilterStatus UdpProxyFilter::StickySessionClusterInfo::onData(Network::UdpRecvData& data) {
  auto active_session = getSession(data.addresses_);
  if (active_session == nullptr) {
    active_session = createSession(std::move(data.addresses_));
    if (active_session == nullptr) {
      return Network::FilterStatus::StopIteration;
    }
  } else {
    if (active_session->host().health() == Upstream::Host::Health::Unhealthy) {
      // If a host becomes unhealthy, we optimally would like to replace it with a new session
      // to a healthy host. We may eventually want to make this behavior configurable, but for now
      // this will be the universal behavior.
      auto host = chooseHost(data.addresses_.peer_);
      if (host != nullptr && host->health() != Upstream::Host::Health::Unhealthy &&
          host.get() != &active_session->host()) {
        ENVOY_LOG(debug, "upstream session unhealthy, recreating the session");
        removeSession(active_session);
        active_session = createSession(std::move(data.addresses_), host);
      } else {
        // In this case we could not get a better host, so just keep using the current session.
        ENVOY_LOG(trace, "upstream session unhealthy, but unable to get a better host");
      }
    }
  }

  active_session->write(*data.buffer_);

  return Network::FilterStatus::StopIteration;
}

UdpProxyFilter::ActiveSession* UdpProxyFilter::StickySessionClusterInfo::getSession(
    const Network::UdpRecvData::LocalPeerAddresses& addresses,
    const HostConstSharedPtrOptConstRef&) const {
  const auto active_session_it = sessions_.find(addresses);
  if (active_session_it != sessions_.end()) {
    return active_session_it->get();
  }
  return nullptr;
}

void UdpProxyFilter::StickySessionClusterInfo::storeSession(ActiveSessionPtr session) {
  sessions_.emplace(std::move(session));
}

void UdpProxyFilter::StickySessionClusterInfo::removeSessionFromStorage(
    const ActiveSession* session) {
  ASSERT(sessions_.count(session) == 1);
  sessions_.erase(session);
}

UdpProxyFilter::PerPacketLoadBalancingClusterInfo::PerPacketLoadBalancingClusterInfo(
    UdpProxyFilter& filter, Upstream::ThreadLocalCluster& cluster)
    : ClusterInfo(filter, cluster) {}

UdpProxyFilter::PerPacketLoadBalancingClusterInfo::~PerPacketLoadBalancingClusterInfo() {
  // Sanity check the session accounting. This is not as fast as a straight teardown, but this is
  // not a performance critical path.
  while (!sessions_.empty()) {
    removeSession(sessions_.begin()->get());
  }
}

Network::FilterStatus
UdpProxyFilter::PerPacketLoadBalancingClusterInfo::onData(Network::UdpRecvData& data) {
  auto host = chooseHost(data.addresses_.peer_);
  if (host == nullptr) {
    ENVOY_LOG(debug, "cannot find any valid host.");
    cluster_.info()->stats().upstream_cx_none_healthy_.inc();
    return Network::FilterStatus::StopIteration;
  }

  ENVOY_LOG(debug, "selected {} host as upstream.", host->address()->asStringView());

  auto active_session = getSession(data.addresses_, host);
  if (active_session == nullptr) {
    active_session = createSession(std::move(data.addresses_), host);
    if (active_session == nullptr) {
      return Network::FilterStatus::StopIteration;
    }
  } else {
    ENVOY_LOG(trace, "found already existing session on host {}.",
              active_session->host().address()->asStringView());
  }

  active_session->write(*data.buffer_);

  return Network::FilterStatus::StopIteration;
}

UdpProxyFilter::ActiveSession* UdpProxyFilter::PerPacketLoadBalancingClusterInfo::getSession(
    const Network::UdpRecvData::LocalPeerAddresses& addresses,
    const HostConstSharedPtrOptConstRef& optional_host) const {
  ASSERT(optional_host);
  LocalPeerHostAddresses key{addresses, *optional_host->get()};
  const auto active_session_it = sessions_.find(key);
  if (active_session_it != sessions_.end()) {
    return active_session_it->get();
  }
  return nullptr;
}

void UdpProxyFilter::PerPacketLoadBalancingClusterInfo::storeSession(ActiveSessionPtr session) {
  sessions_.emplace(std::move(session));
}

void UdpProxyFilter::PerPacketLoadBalancingClusterInfo::removeSessionFromStorage(
    const ActiveSession* session) {
  ASSERT(sessions_.count(session) == 1);
  sessions_.erase(session);
}

UdpProxyFilter::ActiveSession::ActiveSession(ClusterInfo& cluster,
                                             Network::UdpRecvData::LocalPeerAddresses&& addresses,
                                             const Upstream::HostConstSharedPtr& host)
    : cluster_(cluster), use_original_src_ip_(cluster_.filter_.config_->usingOriginalSrcIp()),
      addresses_(std::move(addresses)), host_(host),
      idle_timer_(cluster.filter_.read_callbacks_->udpListener().dispatcher().createTimer(
          [this] { onIdleTimer(); })),
      // NOTE: The socket call can only fail due to memory/fd exhaustion. No local ephemeral port
      //       is bound until the first packet is sent to the upstream host.
      socket_(cluster.filter_.createSocket(host)) {

  socket_->ioHandle().initializeFileEvent(
      cluster.filter_.read_callbacks_->udpListener().dispatcher(),
      [this](uint32_t) { onReadReady(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);
  ENVOY_LOG(debug, "creating new session: downstream={} local={} upstream={}",
            addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host->address()->asStringView());
  cluster_.filter_.config_->stats().downstream_sess_total_.inc();
  cluster_.filter_.config_->stats().downstream_sess_active_.inc();
  cluster_.cluster_.info()
      ->resourceManager(Upstream::ResourcePriority::Default)
      .connections()
      .inc();

  if (use_original_src_ip_) {
    const Network::Socket::OptionsSharedPtr socket_options =
        Network::SocketOptionFactory::buildIpTransparentOptions();
    const bool ok = Network::Socket::applyOptions(
        socket_options, *socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND);

    RELEASE_ASSERT(ok, "Should never occur!");
    ENVOY_LOG(debug, "The original src is enabled for address {}.",
              addresses_.peer_->asStringView());
  }

  // TODO(mattklein123): Enable dropped packets socket option. In general the Socket abstraction
  // does not work well right now for client sockets. It's too heavy weight and is aimed at listener
  // sockets. We need to figure out how to either refactor Socket into something that works better
  // for this use case or allow the socket option abstractions to work directly against an IO
  // handle.
}

UdpProxyFilter::ActiveSession::~ActiveSession() {
  ENVOY_LOG(debug, "deleting the session: downstream={} local={} upstream={}",
            addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_->address()->asStringView());
  cluster_.filter_.config_->stats().downstream_sess_active_.dec();
  cluster_.cluster_.info()
      ->resourceManager(Upstream::ResourcePriority::Default)
      .connections()
      .dec();
}

void UdpProxyFilter::ActiveSession::onIdleTimer() {
  ENVOY_LOG(debug, "session idle timeout: downstream={} local={}", addresses_.peer_->asStringView(),
            addresses_.local_->asStringView());
  cluster_.filter_.config_->stats().idle_timeout_.inc();
  cluster_.removeSession(this);
}

void UdpProxyFilter::ActiveSession::onReadReady() {
  idle_timer_->enableTimer(cluster_.filter_.config_->sessionTimeout());

  // TODO(mattklein123): We should not be passing *addresses_.local_ to this function as we are
  //                     not trying to populate the local address for received packets.
  uint32_t packets_dropped = 0;
  const Api::IoErrorPtr result = Network::Utility::readPacketsFromSocket(
      socket_->ioHandle(), *addresses_.local_, *this, cluster_.filter_.config_->timeSource(),
      cluster_.filter_.config_->upstreamSocketConfig().prefer_gro_, packets_dropped);
  if (result == nullptr) {
    socket_->ioHandle().activateFileEvents(Event::FileReadyType::Read);
    return;
  }
  if (result->getErrorCode() != Api::IoError::IoErrorCode::Again) {
    cluster_.cluster_stats_.sess_rx_errors_.inc();
  }
  // Flush out buffered data at the end of IO event.
  cluster_.filter_.read_callbacks_->udpListener().flush();
}

void UdpProxyFilter::ActiveSession::write(const Buffer::Instance& buffer) {
  ENVOY_LOG(trace, "writing {} byte datagram upstream: downstream={} local={} upstream={}",
            buffer.length(), addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_->address()->asStringView());
  const uint64_t buffer_length = buffer.length();
  cluster_.filter_.config_->stats().downstream_sess_rx_bytes_.add(buffer_length);
  cluster_.filter_.config_->stats().downstream_sess_rx_datagrams_.inc();

  idle_timer_->enableTimer(cluster_.filter_.config_->sessionTimeout());

  // NOTE: On the first write, a local ephemeral port is bound, and thus this write can fail due to
  //       port exhaustion.
  // NOTE: We do not specify the local IP to use for the sendmsg call if use_original_src_ip_ is not
  //       set. We allow the OS to select the right IP based on outbound routing rules if
  //       use_original_src_ip_ is not set, else use downstream peer IP as local IP.
  const Network::Address::Ip* local_ip = use_original_src_ip_ ? addresses_.peer_->ip() : nullptr;
  Api::IoCallUint64Result rc =
      Network::Utility::writeToSocket(socket_->ioHandle(), buffer, local_ip, *host_->address());
  if (!rc.ok()) {
    cluster_.cluster_stats_.sess_tx_errors_.inc();
  } else {
    cluster_.cluster_stats_.sess_tx_datagrams_.inc();
    cluster_.cluster_.info()->stats().upstream_cx_tx_bytes_total_.add(buffer_length);
  }
}

void UdpProxyFilter::ActiveSession::processPacket(Network::Address::InstanceConstSharedPtr,
                                                  Network::Address::InstanceConstSharedPtr,
                                                  Buffer::InstancePtr buffer, MonotonicTime) {
  ENVOY_LOG(trace, "writing {} byte datagram downstream: downstream={} local={} upstream={}",
            buffer->length(), addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_->address()->asStringView());
  const uint64_t buffer_length = buffer->length();

  cluster_.cluster_stats_.sess_rx_datagrams_.inc();
  cluster_.cluster_.info()->stats().upstream_cx_rx_bytes_total_.add(buffer_length);

  Network::UdpSendData data{addresses_.local_->ip(), *addresses_.peer_, *buffer};
  const Api::IoCallUint64Result rc = cluster_.filter_.read_callbacks_->udpListener().send(data);
  if (!rc.ok()) {
    cluster_.filter_.config_->stats().downstream_sess_tx_errors_.inc();
  } else {
    cluster_.filter_.config_->stats().downstream_sess_tx_bytes_.add(buffer_length);
    cluster_.filter_.config_->stats().downstream_sess_tx_datagrams_.inc();
  }
}

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
