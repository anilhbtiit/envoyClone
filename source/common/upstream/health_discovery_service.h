#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/service/discovery/v2/hds.pb.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/upstream.h"

#include "common/common/logger.h"
#include "common/grpc/async_client_impl.h"
#include "common/network/resolver_impl.h"
#include "common/upstream/health_checker_impl.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

class ProdClusterInfoFactory : public ClusterInfoFactory, Logger::Loggable<Logger::Id::upstream> {
public:
  ClusterInfoConstSharedPtr
  createClusterInfo(Runtime::Loader& runtime, const envoy::api::v2::Cluster& cluster,
                    const envoy::api::v2::core::BindConfig& bind_config, Stats::Store& stats,
                    Ssl::ContextManager& ssl_context_manager, Secret::SecretManager& secret_manager,
                    bool added_via_api) override;
};

/**
 * Implementation of Upstream::Cluster for hds clusters, clusters that are used
 * by HdsDelegates
 */

class HdsCluster : public Cluster, Logger::Loggable<Logger::Id::upstream> {
public:
  static ClusterSharedPtr create();
  HdsCluster(Runtime::Loader& runtime, const envoy::api::v2::Cluster& cluster,
             const envoy::api::v2::core::BindConfig& bind_config, Stats::Store& stats,
             Ssl::ContextManager& ssl_context_manager, Secret::SecretManager& secret_manager,
             bool added_via_api, ClusterInfoFactory& info_factory);

  // From Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }
  PrioritySet& prioritySet() override { return priority_set_; }
  const PrioritySet& prioritySet() const override { return priority_set_; }
  void setOutlierDetector(const Outlier::DetectorSharedPtr& outlier_detector);
  HealthChecker* healthChecker() override { return health_checker_.get(); }
  ClusterInfoConstSharedPtr info() const override { return info_; }
  Outlier::Detector* outlierDetector() override { return outlier_detector_.get(); }
  const Outlier::Detector* outlierDetector() const override { return outlier_detector_.get(); }
  void initialize(std::function<void()> callback) override;

  void startHealthchecks(AccessLog::AccessLogManager& access_log_manager, Runtime::Loader& runtime,
                         Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher);

  std::vector<Upstream::HealthCheckerSharedPtr> healthCheckers() { return health_checkers_; };

protected:
  PrioritySetImpl priority_set_;
  HealthCheckerSharedPtr health_checker_;
  Outlier::DetectorSharedPtr outlier_detector_;
  Runtime::Loader& runtime_;

  // Creates a vector containing any healthy hosts
  static HostVectorConstSharedPtr createHealthyHostList(const HostVector& hosts);

private:
  std::function<void()> initialization_complete_callback_;
  ClusterInfoConstSharedPtr info_;
  const envoy::api::v2::Cluster& cluster_;
  const envoy::api::v2::core::BindConfig& bind_config_;
  Stats::Store& stats_;
  Ssl::ContextManager& ssl_context_manager_;
  Secret::SecretManager& secret_manager_;
  bool added_via_api_;
  HostVectorSharedPtr initial_hosts_;
  std::vector<Upstream::HealthCheckerSharedPtr> health_checkers_;
};

typedef std::shared_ptr<HdsCluster> HdsClusterPtr;

/**
 * All hds stats. @see stats_macros.h
 */
// clang-format off
#define ALL_HDS_STATS(COUNTER)                                                           \
  COUNTER(requests)                                                                                \
  COUNTER(responses)                                                                               \
  COUNTER(errors)
// clang-format on

/**
 * Struct definition for all hds stats. @see stats_macros.h
 */
struct HdsDelegateStats {
  ALL_HDS_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * The HdsDelegate class is responsible for receiving requests from a management
 * server with a set of hosts to healthcheck, healthchecking them, and reporting
 * back the results.
 */

class HdsDelegate
    : Grpc::TypedAsyncStreamCallbacks<envoy::service::discovery::v2::HealthCheckSpecifier>,
      Logger::Loggable<Logger::Id::upstream> {
public:
  HdsDelegate(const envoy::api::v2::core::Node& node, Stats::Scope& scope,
              Grpc::AsyncClientPtr async_client, Event::Dispatcher& dispatcher,
              Runtime::Loader& runtime, Envoy::Stats::Store& stats,
              Ssl::ContextManager& ssl_context_manager, Secret::SecretManager& secret_manager,
              Runtime::RandomGenerator& random, ClusterInfoFactory& info_factory,
              AccessLog::AccessLogManager& access_log_manager);

  // Grpc::TypedAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) override;
  void onReceiveMessage(
      std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier>&& message) override;
  void onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;
  envoy::service::discovery::v2::HealthCheckRequestOrEndpointHealthResponse sendResponse();
  // TODO(htuch): Make this configurable or some static.

  // How often we retry to establish a stream
  const uint32_t RetryDelayMilliseconds = 5000;

  // Soft limit on size of the cluster’s connections read and write buffers.
  static constexpr uint32_t ClusterConnectionBufferLimitBytes = 12345;

  // The timeout for new network connections to hosts in the cluster.
  static constexpr uint32_t ClusterTimeoutSeconds = 1;

  // How often envoy reports the healthcheck results to the server
  uint32_t server_response_ms_ = 1000;

  // Processes the management server requests
  void
  processMessage(std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier>&& message);

  // Establishes a connection with the management server
  void establishNewStream();

  std::vector<HdsClusterPtr> hdsClusters() { return hds_clusters_; };

private:
  void setRetryTimer();
  void setServerResponseTimer();
  void handleFailure();
  HdsDelegateStats stats_;
  Grpc::AsyncClientPtr async_client_;
  Grpc::AsyncStream* stream_{};
  const Protobuf::MethodDescriptor& service_method_;
  Event::TimerPtr hds_retry_timer_;
  envoy::service::discovery::v2::HealthCheckRequest health_check_request_;
  std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier> health_check_message_;
  std::vector<std::string> clusters_;
  Runtime::Loader& runtime_;
  Envoy::Stats::Store& store_stats;
  Ssl::ContextManager& ssl_context_manager_;
  Secret::SecretManager& secret_manager_;
  Runtime::RandomGenerator& random_;
  Event::Dispatcher& dispatcher_;

  Event::TimerPtr hds_stream_response_timer_;
  std::vector<HdsClusterPtr> hds_clusters_;
  ClusterInfoFactory& info_factory_;
  AccessLog::AccessLogManager& access_log_manager_;
};

typedef std::unique_ptr<HdsDelegate> HdsDelegatePtr;

} // namespace Upstream
} // namespace Envoy
