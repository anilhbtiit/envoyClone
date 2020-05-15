#pragma once

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/core/v3/health_check.pb.validate.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/upstream/upstream.h"

#include "common/common/utility.h"
#include "common/json/json_loader.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/test_common/utility.h"

#include "fmt/printf.h"

namespace Envoy {
namespace Upstream {
namespace {

constexpr static const char* kDefaultStaticClusterTmpl = R"EOF(
  {
    "name": "%s",
    "connect_timeout": "0.250s",
    "type": "static",
    "lb_policy": "round_robin",
    "load_assignment": {
    "endpoints": [
      {
        "lb_endpoints": [
          {
            "endpoint":
            {
              "address":
              {
                  %s  
               }
            }
            
          }
        ]
      }
    ]
  }
  }
  )EOF";

inline std::string defaultStaticClusterJson(const std::string& name) {
  return fmt::sprintf(kDefaultStaticClusterTmpl, name, R"EOF(
"socket_address": {
  "address": "127.0.0.1",
  "port_value": 11001
})EOF");
}

inline envoy::config::bootstrap::v3::Bootstrap
parseBootstrapFromV2Json(const std::string& json_string) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromJson(json_string, bootstrap, true);
  return bootstrap;
}

inline envoy::config::cluster::v3::Cluster parseClusterFromV2Json(const std::string& json_string) {
  envoy::config::cluster::v3::Cluster cluster;
  TestUtility::loadFromJson(json_string, cluster, true);
  return cluster;
}

inline envoy::config::cluster::v3::Cluster parseClusterFromV2Yaml(const std::string& yaml) {
  envoy::config::cluster::v3::Cluster cluster;
  TestUtility::loadFromYaml(yaml, cluster, true);
  return cluster;
}

inline envoy::config::cluster::v3::Cluster defaultStaticCluster(const std::string& name) {
  return parseClusterFromV2Json(defaultStaticClusterJson(name));
}

inline HostSharedPtr makeTestHost(ClusterInfoConstSharedPtr cluster, const std::string& hostname,
                                  const std::string& url, uint32_t weight = 1) {
  return HostSharedPtr{
      new HostImpl(cluster, hostname, Network::Utility::resolveUrl(url), nullptr, weight,
                   envoy::config::core::v3::Locality(),
                   envoy::config::endpoint::v3::Endpoint::HealthCheckConfig::default_instance(), 0,
                   envoy::config::core::v3::UNKNOWN)};
}

inline HostSharedPtr makeTestHost(ClusterInfoConstSharedPtr cluster, const std::string& url,
                                  uint32_t weight = 1) {
  return HostSharedPtr{
      new HostImpl(cluster, "", Network::Utility::resolveUrl(url), nullptr, weight,
                   envoy::config::core::v3::Locality(),
                   envoy::config::endpoint::v3::Endpoint::HealthCheckConfig::default_instance(), 0,
                   envoy::config::core::v3::UNKNOWN)};
}

inline HostSharedPtr makeTestHost(ClusterInfoConstSharedPtr cluster, const std::string& url,
                                  const envoy::config::core::v3::Metadata& metadata,
                                  uint32_t weight = 1) {
  return HostSharedPtr{
      new HostImpl(cluster, "", Network::Utility::resolveUrl(url),
                   std::make_shared<const envoy::config::core::v3::Metadata>(metadata), weight,
                   envoy::config::core::v3::Locality(),
                   envoy::config::endpoint::v3::Endpoint::HealthCheckConfig::default_instance(), 0,
                   envoy::config::core::v3::UNKNOWN)};
}

inline HostSharedPtr
makeTestHost(ClusterInfoConstSharedPtr cluster, const std::string& url,
             const envoy::config::endpoint::v3::Endpoint::HealthCheckConfig& health_check_config,
             uint32_t weight = 1) {
  return HostSharedPtr{new HostImpl(cluster, "", Network::Utility::resolveUrl(url), nullptr, weight,
                                    envoy::config::core::v3::Locality(), health_check_config, 0,
                                    envoy::config::core::v3::UNKNOWN)};
}

inline HostDescriptionConstSharedPtr makeTestHostDescription(ClusterInfoConstSharedPtr cluster,
                                                             const std::string& url) {
  return HostDescriptionConstSharedPtr{new HostDescriptionImpl(
      cluster, "", Network::Utility::resolveUrl(url), nullptr,
      envoy::config::core::v3::Locality().default_instance(),
      envoy::config::endpoint::v3::Endpoint::HealthCheckConfig::default_instance(), 0)};
}

inline HostsPerLocalitySharedPtr makeHostsPerLocality(std::vector<HostVector>&& locality_hosts,
                                                      bool force_no_local_locality = false) {
  return std::make_shared<HostsPerLocalityImpl>(
      std::move(locality_hosts), !force_no_local_locality && !locality_hosts.empty());
}

inline LocalityWeightsSharedPtr
makeLocalityWeights(std::initializer_list<uint32_t> locality_weights) {
  return std::make_shared<LocalityWeights>(locality_weights);
}

inline envoy::config::core::v3::HealthCheck
parseHealthCheckFromV3Yaml(const std::string& yaml_string) {
  envoy::config::core::v3::HealthCheck health_check;
  TestUtility::loadFromYamlAndValidate(yaml_string, health_check);
  return health_check;
}

inline PrioritySet::UpdateHostsParams
updateHostsParams(HostVectorConstSharedPtr hosts, HostsPerLocalityConstSharedPtr hosts_per_locality,
                  HealthyHostVectorConstSharedPtr healthy_hosts,
                  HostsPerLocalityConstSharedPtr healthy_hosts_per_locality) {
  return HostSetImpl::updateHostsParams(
      hosts, hosts_per_locality, std::move(healthy_hosts), std::move(healthy_hosts_per_locality),
      std::make_shared<const DegradedHostVector>(), HostsPerLocalityImpl::empty(),
      std::make_shared<const ExcludedHostVector>(), HostsPerLocalityImpl::empty());
}

inline PrioritySet::UpdateHostsParams
updateHostsParams(HostVectorConstSharedPtr hosts,
                  HostsPerLocalityConstSharedPtr hosts_per_locality) {
  return updateHostsParams(std::move(hosts), std::move(hosts_per_locality),
                           std::make_shared<const HealthyHostVector>(),
                           HostsPerLocalityImpl::empty());
}

} // namespace
} // namespace Upstream
} // namespace Envoy
