#pragma once

#include "envoy/config/subscription.h"
#include "envoy/local_info/local_info.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/protobuf/protobuf.h"

#include "api/base.pb.h"
#include "api/filter/http_connection_manager.pb.h"

namespace Envoy {
namespace Config {

/**
 * General config API utilities.
 */
class Utility {
public:
  /**
   * Extract typed resources from a DiscoveryResponse.
   * @param response reference to DiscoveryResponse.
   * @return Protobuf::RepatedPtrField<ResourceType> vector of typed resources in response.
   */
  template <class ResourceType>
  static Protobuf::RepeatedPtrField<ResourceType>
  getTypedResources(const envoy::api::v2::DiscoveryResponse& response) {
    Protobuf::RepeatedPtrField<ResourceType> typed_resources;
    for (auto& resource : response.resources()) {
      auto* typed_resource = typed_resources.Add();
      resource.UnpackTo(typed_resource);
    }
    return typed_resources;
  }

  /**
   * Extract refresh_delay as a std::chrono::milliseconds from envoy::api::v2::ApiConfigSource.
   */
  static std::chrono::milliseconds
  apiConfigSourceRefreshDelay(const envoy::api::v2::ApiConfigSource& api_config_source);

  /**
   * Check cluster info for API config sanity. Throws on error.
   * @param error_prefix supplies the prefix to use in error messages.
   * @param cluster_name supplies the cluster name to check.
   * @param cm supplies the cluster manager.
   */
  static void checkCluster(const std::string& error_prefix, const std::string& cluster_name,
                           Upstream::ClusterManager& cm);

  /**
   * Check cluster/local info for API config sanity. Throws on error.
   * @param error_prefix supplies the prefix to use in error messages.
   * @param cluster_name supplies the cluster name to check.
   * @param cm supplies the cluster manager.
   * @param local_info supplies the local info.
   */
  static void checkClusterAndLocalInfo(const std::string& error_prefix,
                                       const std::string& cluster_name,
                                       Upstream::ClusterManager& cm,
                                       const LocalInfo::LocalInfo& local_info);

  /**
   * Check local info for API config sanity. Throws on error.
   * @param error_prefix supplies the prefix to use in error messages.
   * @param local_info supplies the local info.
   */
  static void checkLocalInfo(const std::string& error_prefix,
                             const LocalInfo::LocalInfo& local_info);

  /**
   * Convert a v1 SdsConfig to v2 EDS envoy::api::v2::ConfigSource.
   * @param sds_config source v1 SdsConfig.
   * @param eds_config destination v2 EDS envoy::api::v2::ConfigSource.
   */
  static void sdsConfigToEdsConfig(const Upstream::SdsConfig& sds_config,
                                   envoy::api::v2::ConfigSource& eds_config);

  /**
   * Convert a v1 CDS JSON config to v2 CDS envoy::api::v2::ConfigSource.
   * @param json_config source v1 CDS JSON config.
   * @param cds_config destination v2 CDS envoy::api::v2::ConfigSource.
   */
  static void translateCdsConfig(const Json::Object& json_config,
                                 envoy::api::v2::ConfigSource& cds_config);

  /**
   * Convert a v1 RDS JSON config to v2 RDS envoy::api::v2::filter::Rds.
   * @param json_rds source v1 RDS JSON config.
   * @param rds destination v2 RDS envoy::api::v2::filter::Rds.
   */
  static void translateRdsConfig(const Json::Object& json_rds, envoy::api::v2::filter::Rds& rds);

  /**
   * Convert a v1 LDS JSON config to v2 LDS envoy::api::v2::ConfigSource.
   * @param json_lds source v1 LDS JSON config.
   * @param lds_config destination v2 LDS envoy::api::v2::ConfigSource.
   */
  static void translateLdsConfig(const Json::Object& json_lds,
                                 envoy::api::v2::ConfigSource& lds_config);

  /**
   * Generate a SubscriptionStats object from stats scope.
   * @param scope for stats.
   * @return SubscriptionStats for scope.
   */
  static SubscriptionStats generateStats(Stats::Scope& scope) {
    return {ALL_SUBSCRIPTION_STATS(POOL_COUNTER(scope))};
  }
};

} // namespace Config
} // namespace Envoy
