#pragma once

#include "envoy/common/time.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * A logical family of hosts, supporting dynamic update. This shares much
 * of its implementation with HostDescriptionImpl, but has non-const address
 * member variables that are lock-protected.
 */
class LogicalHostDescription : public HostDescriptionImplBase {
public:
  LogicalHostDescription(
      ClusterInfoConstSharedPtr cluster, const std::string& hostname,
      Network::Address::InstanceConstSharedPtr dest_address, MetadataConstSharedPtr metadata,
      const envoy::config::core::v3::Locality& locality,
      const envoy::config::endpoint::v3::Endpoint::HealthCheckConfig& health_check_config,
      uint32_t priority, TimeSource& time_source)
      : HostDescriptionImplBase(cluster, hostname, dest_address, metadata, locality,
                                health_check_config, priority, time_source),
        address_(dest_address) {}

  // HostDescription
  Network::Address::InstanceConstSharedPtr healthCheckAddress() const override;

protected:
  // The first entry in the address_list_ should match the value in address_.
  Network::Address::InstanceConstSharedPtr address_ ABSL_GUARDED_BY(address_lock_);
  SharedConstAddressVector address_list_or_null_ ABSL_GUARDED_BY(address_lock_);
  Network::Address::InstanceConstSharedPtr health_check_address_ ABSL_GUARDED_BY(address_lock_);
  mutable absl::Mutex address_lock_;
};

/**
 * A host implementation that can have its address changed in order to create temporal "real"
 * hosts.
 */
class LogicalHost : public HostImplBase, public LogicalHostDescription {
public:
  LogicalHost(
      const ClusterInfoConstSharedPtr& cluster, const std::string& hostname,
      const Network::Address::InstanceConstSharedPtr& address, const AddressVector& address_list,
      const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint,
      const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint,
      const Network::TransportSocketOptionsConstSharedPtr& override_transport_socket_options,
      TimeSource& time_source);

  /**
   * Sets new addresses. This can be called dynamically during operation, and
   * is thread-safe.
   *
   * TODO: the health checker only gets the first address in the list and will
   * not walk the full happy eyeballs list. We should eventually fix this.
   *
   * TODO(jmarantz): change call-site to pass the address_list as a shared_ptr to
   * avoid having to copy it.
   *
   * @param address the primary address, also used for health checking
   * @param address_list alternative addresses; the first of these must be 'address'
   * @param lb_endpoint the load-balanced endpoint
   */
  void setNewAddresses(const Network::Address::InstanceConstSharedPtr& address,
                       const AddressVector& address_list,
                       const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint);

  // Upstream::Host
  CreateConnectionData createConnection(
      Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
      Network::TransportSocketOptionsConstSharedPtr transport_socket_options) const override;

  // Upstream::HostDescription
  SharedConstAddressVector addressListOrNull() const override;
  Network::Address::InstanceConstSharedPtr address() const override;

private:
  const Network::TransportSocketOptionsConstSharedPtr override_transport_socket_options_;
};

using LogicalHostSharedPtr = std::shared_ptr<LogicalHost>;

/**
 * A real host that forwards most of its calls to a logical host, but returns a snapped address.
 */
class RealHostDescription : public HostDescription {
public:
  RealHostDescription(Network::Address::InstanceConstSharedPtr address,
                      HostConstSharedPtr logical_host)
      : address_(address), logical_host_(logical_host) {}

  // Upstream:HostDescription
  bool canary() const override { return logical_host_->canary(); }
  void canary(bool) override {}
  MetadataConstSharedPtr metadata() const override { return logical_host_->metadata(); }
  void metadata(MetadataConstSharedPtr) override {}

  Network::UpstreamTransportSocketFactory& transportSocketFactory() const override {
    return logical_host_->transportSocketFactory();
  }
  const ClusterInfo& cluster() const override { return logical_host_->cluster(); }
  bool canCreateConnection(Upstream::ResourcePriority priority) const override {
    return logical_host_->canCreateConnection(priority);
  }
  HealthCheckHostMonitor& healthChecker() const override { return logical_host_->healthChecker(); }
  void setHealthChecker(HealthCheckHostMonitorPtr&&) override {}
  void setOutlierDetector(Outlier::DetectorHostMonitorPtr&&) override {}
  Outlier::DetectorHostMonitor& outlierDetector() const override {
    return logical_host_->outlierDetector();
  }
  HostStats& stats() const override { return logical_host_->stats(); }
  LoadMetricStats& loadMetricStats() const override { return logical_host_->loadMetricStats(); }
  const std::string& hostnameForHealthChecks() const override {
    return logical_host_->hostnameForHealthChecks();
  }
  const std::string& hostname() const override { return logical_host_->hostname(); }
  Network::Address::InstanceConstSharedPtr address() const override { return address_; }
  SharedConstAddressVector addressListOrNull() const override {
    return logical_host_->addressListOrNull();
  }
  const envoy::config::core::v3::Locality& locality() const override {
    return logical_host_->locality();
  }
  Stats::StatName localityZoneStatName() const override {
    return logical_host_->localityZoneStatName();
  }
  Network::Address::InstanceConstSharedPtr healthCheckAddress() const override {
    // Should never be called since real hosts are used only for forwarding.
    return nullptr;
  }
  absl::optional<MonotonicTime> lastHcPassTime() const override {
    return logical_host_->lastHcPassTime();
  }
  void setLastHcPassTime(MonotonicTime) override {}
  uint32_t priority() const override { return logical_host_->priority(); }
  void priority(uint32_t) override {}

  Network::UpstreamTransportSocketFactory&
  resolveTransportSocketFactory(const Network::Address::InstanceConstSharedPtr& dest_address,
                                const envoy::config::core::v3::Metadata* metadata) const override {
    return logical_host_->resolveTransportSocketFactory(dest_address, metadata);
  }

private:
  const Network::Address::InstanceConstSharedPtr address_;
  const HostConstSharedPtr logical_host_;
};

} // namespace Upstream
} // namespace Envoy
