#include "source/common/network/utility.h"
#include "source/common/upstream/host_utility.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Contains;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::UnorderedElementsAreArray;

namespace Envoy {
namespace Upstream {
namespace {

static constexpr HostUtility::HostStatusSet UnknownStatus =
    1u << static_cast<uint32_t>(envoy::config::core::v3::HealthStatus::UNKNOWN);
static constexpr HostUtility::HostStatusSet HealthyStatus =
    1u << static_cast<uint32_t>(envoy::config::core::v3::HealthStatus::HEALTHY);
static constexpr HostUtility::HostStatusSet UnhealthyStatus =
    1u << static_cast<uint32_t>(envoy::config::core::v3::HealthStatus::UNHEALTHY);
static constexpr HostUtility::HostStatusSet DrainingStatus =
    1u << static_cast<uint32_t>(envoy::config::core::v3::HealthStatus::DRAINING);
static constexpr HostUtility::HostStatusSet TimeoutStatus =
    1u << static_cast<uint32_t>(envoy::config::core::v3::HealthStatus::TIMEOUT);
static constexpr HostUtility::HostStatusSet DegradedStatus =
    1u << static_cast<uint32_t>(envoy::config::core::v3::HealthStatus::DEGRADED);

TEST(HostUtilityTest, All) {
  auto cluster = std::make_shared<NiceMock<MockClusterInfo>>();
  auto time_source = std::make_unique<NiceMock<MockTimeSystem>>();
  HostSharedPtr host = makeTestHost(cluster, "tcp://127.0.0.1:80", *time_source);
  EXPECT_EQ("healthy", HostUtility::healthFlagsToString(*host));

  host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_EQ("/failed_active_hc", HostUtility::healthFlagsToString(*host));

  host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  EXPECT_EQ("/failed_active_hc/failed_outlier_check", HostUtility::healthFlagsToString(*host));

  host->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_EQ("/failed_outlier_check", HostUtility::healthFlagsToString(*host));

  host->healthFlagSet(Host::HealthFlag::FAILED_EDS_HEALTH);
  EXPECT_EQ("/failed_outlier_check/failed_eds_health", HostUtility::healthFlagsToString(*host));

  host->healthFlagClear(Host::HealthFlag::FAILED_EDS_HEALTH);
  EXPECT_EQ("/failed_outlier_check", HostUtility::healthFlagsToString(*host));

  // Invokes healthFlagSet for each health flag.
#define SET_HEALTH_FLAG(name, notused) host->healthFlagSet(Host::HealthFlag::name);
  HEALTH_FLAG_ENUM_VALUES(SET_HEALTH_FLAG)
#undef SET_HEALTH_FLAG
  EXPECT_EQ("/failed_active_hc/failed_outlier_check/failed_eds_health/degraded_active_hc/"
            "degraded_eds_health/pending_dynamic_removal/pending_active_hc/"
            "excluded_via_immediate_hc_fail/active_hc_timeout",
            HostUtility::healthFlagsToString(*host));
}

TEST(HostLogging, FmtUtils) {
  auto cluster = std::make_shared<NiceMock<MockClusterInfo>>();
  auto time_source = std::make_unique<NiceMock<MockTimeSystem>>();
  auto time_ms = std::chrono::milliseconds(5);
  ON_CALL(*time_source, monotonicTime()).WillByDefault(Return(MonotonicTime(time_ms)));
  EXPECT_LOG_CONTAINS("warn", "Logging host info 127.0.0.1:80 end", {
    HostSharedPtr host = makeTestHost(cluster, "tcp://127.0.0.1:80", *time_source);
    ENVOY_LOG_MISC(warn, "Logging host info {} end", *host);
  });
  EXPECT_LOG_CONTAINS("warn", "Logging host info hostname end", {
    HostSharedPtr host = makeTestHost(cluster, "hostname", "tcp://127.0.0.1:80", *time_source);
    ENVOY_LOG_MISC(warn, "Logging host info {} end", *host);
  });
}

TEST(HostUtilityTest, CreateOverrideHostStatus) {

  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNKNOWN);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::HEALTHY);
    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config), UnknownStatus | HealthyStatus);
  }
  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNHEALTHY);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DRAINING);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::TIMEOUT);

    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config),
              UnhealthyStatus | DrainingStatus | TimeoutStatus);
  }
  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DEGRADED);
    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config), DegradedStatus);
  }
  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config),
              UnknownStatus | HealthyStatus | DegradedStatus);
  }
  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNHEALTHY);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DRAINING);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::TIMEOUT);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNKNOWN);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::HEALTHY);

    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config),
              UnhealthyStatus | DrainingStatus | TimeoutStatus | UnknownStatus | HealthyStatus);
  }

  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNHEALTHY);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DRAINING);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::TIMEOUT);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNKNOWN);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::HEALTHY);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DEGRADED);
    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config),
              UnknownStatus | HealthyStatus | UnhealthyStatus | DrainingStatus | TimeoutStatus |
                  DegradedStatus);
  }
}

TEST(HostUtilityTest, SelectOverrideHostTest) {

  NiceMock<Upstream::MockLoadBalancerContext> context;

  const HostUtility::HostStatusSet all_health_statuses = UnknownStatus | HealthyStatus |
                                                         UnhealthyStatus | DrainingStatus |
                                                         TimeoutStatus | DegradedStatus;

  {
    // No valid host map.
    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(nullptr, all_health_statuses, &context));
  }
  {
    // No valid load balancer context.
    auto host_map = std::make_shared<HostMap>();
    EXPECT_EQ(nullptr,
              HostUtility::selectOverrideHost(host_map.get(), all_health_statuses, nullptr));
  }
  {
    // No valid expected host.
    EXPECT_CALL(context, overrideHostToSelect()).WillOnce(Return(absl::nullopt));
    auto host_map = std::make_shared<HostMap>();
    EXPECT_EQ(nullptr,
              HostUtility::selectOverrideHost(host_map.get(), all_health_statuses, &context));
  }
  {
    // The host map does not contain the expected host.
    LoadBalancerContext::OverrideHost override_host{"1.2.3.4"};
    EXPECT_CALL(context, overrideHostToSelect())
        .WillOnce(Return(absl::make_optional(override_host)));
    auto host_map = std::make_shared<HostMap>();
    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), HealthyStatus, &context));
  }
  {
    auto mock_host = std::make_shared<NiceMock<MockHost>>();
    EXPECT_CALL(*mock_host, healthStatus())
        .WillRepeatedly(Return(envoy::config::core::v3::HealthStatus::UNHEALTHY));

    LoadBalancerContext::OverrideHost override_host{"1.2.3.4"};
    EXPECT_CALL(context, overrideHostToSelect())
        .WillRepeatedly(Return(absl::make_optional(override_host)));

    auto host_map = std::make_shared<HostMap>();
    host_map->insert({"1.2.3.4", mock_host});

    EXPECT_EQ(mock_host,
              HostUtility::selectOverrideHost(host_map.get(), UnhealthyStatus, &context));
    EXPECT_EQ(mock_host,
              HostUtility::selectOverrideHost(host_map.get(), all_health_statuses, &context));

    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), HealthyStatus, &context));
    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), DegradedStatus, &context));
    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), TimeoutStatus, &context));
    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), DrainingStatus, &context));
    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), UnknownStatus, &context));
  }
  {
    auto mock_host = std::make_shared<NiceMock<MockHost>>();
    EXPECT_CALL(*mock_host, healthStatus())
        .WillRepeatedly(Return(envoy::config::core::v3::HealthStatus::DEGRADED));

    LoadBalancerContext::OverrideHost override_host{"1.2.3.4"};
    EXPECT_CALL(context, overrideHostToSelect())
        .WillRepeatedly(Return(absl::make_optional(override_host)));

    auto host_map = std::make_shared<HostMap>();
    host_map->insert({"1.2.3.4", mock_host});
    EXPECT_EQ(mock_host, HostUtility::selectOverrideHost(host_map.get(), DegradedStatus, &context));
    EXPECT_EQ(mock_host,
              HostUtility::selectOverrideHost(host_map.get(), all_health_statuses, &context));

    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), HealthyStatus, &context));
    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), UnhealthyStatus, &context));
    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), TimeoutStatus, &context));
    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), DrainingStatus, &context));
    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), UnknownStatus, &context));
  }
}

TEST(HostUtilityTest, CreateOverrideHostStatusWithRuntimeFlagFlase) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.validate_detailed_override_host_statuses", "false"}});

  // Deprecated status that will be removed after runtime flag is removed.
  static constexpr HostUtility::HostStatusSet UnhealthyStatus =
      1u << static_cast<size_t>(Host::Health::Unhealthy);
  static constexpr HostUtility::HostStatusSet DegradedStatus =
      1u << static_cast<size_t>(Host::Health::Degraded);
  static constexpr HostUtility::HostStatusSet HealthyStatus =
      1u << static_cast<size_t>(Host::Health::Healthy);

  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNKNOWN);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::HEALTHY);
    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config), HealthyStatus);
  }
  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNHEALTHY);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DRAINING);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::TIMEOUT);

    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config), UnhealthyStatus);
  }
  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DEGRADED);
    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config), DegradedStatus);
  }
  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config), 0b110u);
  }
  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNHEALTHY);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DRAINING);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::TIMEOUT);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNKNOWN);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::HEALTHY);

    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config), 0b101u);
  }

  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNHEALTHY);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DRAINING);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::TIMEOUT);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNKNOWN);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::HEALTHY);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DEGRADED);
    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config), 0b111u);
  }
}

TEST(HostUtilityTest, SelectOverrideHostTestRuntimeFlagFlase) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.validate_detailed_override_host_statuses", "false"}});

  // Deprecated status that will be removed after runtime flag is removed.
  static constexpr HostUtility::HostStatusSet UnhealthyStatus =
      1u << static_cast<size_t>(Host::Health::Unhealthy);
  static constexpr HostUtility::HostStatusSet DegradedStatus =
      1u << static_cast<size_t>(Host::Health::Degraded);
  static constexpr HostUtility::HostStatusSet HealthyStatus =
      1u << static_cast<size_t>(Host::Health::Healthy);

  NiceMock<Upstream::MockLoadBalancerContext> context;

  const HostUtility::HostStatusSet all_health_statuses =
      UnhealthyStatus | DegradedStatus | HealthyStatus;

  {
    // No valid host map.
    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(nullptr, all_health_statuses, &context));
  }
  {
    // No valid load balancer context.
    auto host_map = std::make_shared<HostMap>();
    EXPECT_EQ(nullptr,
              HostUtility::selectOverrideHost(host_map.get(), all_health_statuses, nullptr));
  }
  {
    // No valid expected host.
    EXPECT_CALL(context, overrideHostToSelect()).WillOnce(Return(absl::nullopt));
    auto host_map = std::make_shared<HostMap>();
    EXPECT_EQ(nullptr,
              HostUtility::selectOverrideHost(host_map.get(), all_health_statuses, &context));
  }
  {
    // The host map does not contain the expected host.
    LoadBalancerContext::OverrideHost override_host{"1.2.3.4"};
    EXPECT_CALL(context, overrideHostToSelect())
        .WillOnce(Return(absl::make_optional(override_host)));
    auto host_map = std::make_shared<HostMap>();
    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), HealthyStatus, &context));
  }
  {
    // The status of host is not as expected.
    auto mock_host = std::make_shared<NiceMock<MockHost>>();
    EXPECT_CALL(*mock_host, coarseHealth()).WillOnce(Return(Host::Health::Unhealthy));

    LoadBalancerContext::OverrideHost override_host{"1.2.3.4"};
    EXPECT_CALL(context, overrideHostToSelect())
        .WillOnce(Return(absl::make_optional(override_host)));

    auto host_map = std::make_shared<HostMap>();
    host_map->insert({"1.2.3.4", mock_host});
    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), HealthyStatus, &context));
  }
  {
    // Get expected host.
    auto mock_host = std::make_shared<NiceMock<MockHost>>();
    EXPECT_CALL(*mock_host, coarseHealth()).WillOnce(Return(Host::Health::Degraded));

    LoadBalancerContext::OverrideHost override_host{"1.2.3.4"};
    EXPECT_CALL(context, overrideHostToSelect())
        .WillOnce(Return(absl::make_optional(override_host)));

    auto host_map = std::make_shared<HostMap>();
    host_map->insert({"1.2.3.4", mock_host});
    EXPECT_EQ(mock_host, HostUtility::selectOverrideHost(host_map.get(),
                                                         HealthyStatus | DegradedStatus, &context));
  }
}

class PerEndpointMetricsTest : public testing::Test {
public:
  MockClusterMockPrioritySet& makeCluster(absl::string_view name, uint32_t num_hosts = 1,
                                          bool warming = false) {
    clusters_.emplace_back(std::make_unique<NiceMock<MockClusterMockPrioritySet>>());
    clusters_.back()->info_->name_ = name;
    ON_CALL(*clusters_.back()->info_, perEndpointStats()).WillByDefault(Return(true));
    ON_CALL(*clusters_.back()->info_, observabilityName())
        .WillByDefault(ReturnRef(clusters_.back()->info_->name_));
    static Stats::TagVector empty_tags;
    ON_CALL(clusters_.back()->info_->stats_store_, fixedTags())
        .WillByDefault(ReturnRef(empty_tags));

    if (warming) {
      cluster_info_maps_.warming_clusters_.emplace(name, *clusters_.back());
    } else {
      cluster_info_maps_.active_clusters_.emplace(name, *clusters_.back());
    }

    addHosts(*clusters_.back(), num_hosts);

    return *clusters_.back();
  }

  MockHost& addHost(MockClusterMockPrioritySet& cluster, uint32_t priority = 0) {
    host_count_++;
    MockHostSet* host_set = cluster.priority_set_.getMockHostSet(priority);
    auto host = std::make_shared<NiceMock<MockHost>>();
    ON_CALL(*host, address())
        .WillByDefault(Return(Network::Utility::parseInternetAddressAndPort(
            fmt::format("127.0.0.{}:80", host_count_))));
    ON_CALL(*host, hostname()).WillByDefault(ReturnRef(EMPTY_STRING));
    ON_CALL(*host, coarseHealth()).WillByDefault(Return(Host::Health::Healthy));

    counters_.emplace_back();
    auto& c1 = counters_.back();
    c1.add((host_count_ * 10) + 1);
    counters_.emplace_back();
    auto& c2 = counters_.back();
    c2.add((host_count_ * 10) + 2);
    gauges_.emplace_back();
    auto& g1 = gauges_.back();
    g1.add((host_count_ * 10) + 3);
    gauges_.emplace_back();
    auto& g2 = gauges_.back();
    g2.add((host_count_ * 10) + 4);

    ON_CALL(*host, counters())
        .WillByDefault(
            Return(std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>>{
                {"c1", c1}, {"c2", c2}}));
    ON_CALL(*host, gauges())
        .WillByDefault(
            Return(std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>>{
                {"g1", g1}, {"g2", g2}}));
    host_set->hosts_.push_back(host);
    return *host;
  }

  void addHosts(MockClusterMockPrioritySet& cluster, uint32_t count = 1) {
    for (uint32_t i = 0; i < count; i++) {
      addHost(cluster);
    }
  }

  std::pair<std::vector<Stats::PrimitiveCounterSnapshot>,
            std::vector<Stats::PrimitiveGaugeSnapshot>>
  run() {
    EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_info_maps_));

    std::vector<Stats::PrimitiveCounterSnapshot> counters;
    std::vector<Stats::PrimitiveGaugeSnapshot> gauges;
    HostUtility::forEachHostMetric(
        cm_,
        [&](Stats::PrimitiveCounterSnapshot&& metric) { counters.emplace_back(std::move(metric)); },
        [&](Stats::PrimitiveGaugeSnapshot&& metric) { gauges.emplace_back(std::move(metric)); });

    return std::make_pair(counters, gauges);
  }

  MockClusterManager cm_;
  ClusterManager::ClusterInfoMaps cluster_info_maps_;
  std::vector<std::unique_ptr<MockClusterMockPrioritySet>> clusters_;
  std::list<Stats::PrimitiveCounter> counters_;
  std::list<Stats::PrimitiveGauge> gauges_;
  uint32_t host_count_{0};
};

template <class MetricType>
std::vector<std::string> metricNames(const std::vector<MetricType>& metrics) {
  std::vector<std::string> names;
  names.reserve(metrics.size());
  for (const auto& metric : metrics) {
    names.push_back(metric.name());
  }
  return names;
}

template <class MetricType>
std::vector<std::pair<std::string, uint64_t>>
metricNamesAndValues(const std::vector<MetricType>& metrics) {
  std::vector<std::pair<std::string, uint64_t>> ret;
  ret.reserve(metrics.size());
  for (const auto& metric : metrics) {
    ret.push_back(std::make_pair(metric.name(), metric.value()));
  }
  return ret;
}

template <class MetricType>
const MetricType& getMetric(absl::string_view name, const std::vector<MetricType>& metrics) {
  for (const auto& metric : metrics) {
    if (metric.name() == name) {
      return metric;
    }
  }
  PANIC("not found");
}

TEST_F(PerEndpointMetricsTest, Basic) {
  makeCluster("mycluster", 1);
  auto [counters, gauges] = run();

  EXPECT_THAT(metricNamesAndValues(counters),
              UnorderedElementsAreArray({
                  std::make_pair("cluster.mycluster.endpoint.127.0.0.1_80.c1", 11),
                  std::make_pair("cluster.mycluster.endpoint.127.0.0.1_80.c2", 12),
              }));
  EXPECT_THAT(metricNamesAndValues(gauges),
              UnorderedElementsAreArray({
                  std::make_pair("cluster.mycluster.endpoint.127.0.0.1_80.g1", 13),
                  std::make_pair("cluster.mycluster.endpoint.127.0.0.1_80.g2", 14),
                  std::make_pair("cluster.mycluster.endpoint.127.0.0.1_80.healthy", 1),
              }));
}

// Warming clusters are not included
TEST_F(PerEndpointMetricsTest, Warming) {
  makeCluster("mycluster", 1);
  makeCluster("warming", 1, true /* warming */);

  auto [counters, gauges] = run();

  EXPECT_THAT(metricNames(counters),
              UnorderedElementsAreArray({"cluster.mycluster.endpoint.127.0.0.1_80.c1",
                                         "cluster.mycluster.endpoint.127.0.0.1_80.c2"}));
  EXPECT_THAT(metricNames(gauges),
              UnorderedElementsAreArray({"cluster.mycluster.endpoint.127.0.0.1_80.g1",
                                         "cluster.mycluster.endpoint.127.0.0.1_80.g2",
                                         "cluster.mycluster.endpoint.127.0.0.1_80.healthy"}));
}

TEST_F(PerEndpointMetricsTest, HealthyGaugeUnhealthy) {
  auto& cluster = makeCluster("mycluster", 0);
  auto& host = addHost(cluster);
  EXPECT_CALL(host, coarseHealth()).WillOnce(Return(Host::Health::Unhealthy));
  auto [counters, gauges] = run();
  EXPECT_EQ(getMetric("cluster.mycluster.endpoint.127.0.0.1_80.healthy", gauges).value(), 0);
}

TEST_F(PerEndpointMetricsTest, HealthyGaugeDegraded) {
  auto& cluster = makeCluster("mycluster", 0);
  auto& host = addHost(cluster);
  EXPECT_CALL(host, coarseHealth()).WillOnce(Return(Host::Health::Degraded));
  auto [counters, gauges] = run();
  EXPECT_EQ(getMetric("cluster.mycluster.endpoint.127.0.0.1_80.healthy", gauges).value(), 0);
}

TEST_F(PerEndpointMetricsTest, MultipleClustersAndHosts) {
  makeCluster("cluster1", 2);
  makeCluster("cluster2", 3);

  auto [counters, gauges] = run();

  EXPECT_THAT(metricNamesAndValues(counters),
              UnorderedElementsAreArray({
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.c1", 11),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.c2", 12),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.c1", 21),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.c2", 22),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.3_80.c1", 31),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.3_80.c2", 32),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.4_80.c1", 41),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.4_80.c2", 42),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.5_80.c1", 51),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.5_80.c2", 52),
              }));
  EXPECT_THAT(metricNamesAndValues(gauges),
              UnorderedElementsAreArray({
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.g1", 13),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.g2", 14),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.healthy", 1),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.g1", 23),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.g2", 24),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.healthy", 1),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.3_80.g1", 33),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.3_80.g2", 34),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.3_80.healthy", 1),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.4_80.g1", 43),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.4_80.g2", 44),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.4_80.healthy", 1),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.5_80.g1", 53),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.5_80.g2", 54),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.5_80.healthy", 1),
              }));
}

TEST_F(PerEndpointMetricsTest, MultiplePriorityLevels) {
  auto& cluster = makeCluster("cluster1", 1);
  addHost(cluster, 2 /* non-default priority level */);

  auto [counters, gauges] = run();

  EXPECT_THAT(metricNamesAndValues(counters),
              UnorderedElementsAreArray({
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.c1", 11),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.c2", 12),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.c1", 21),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.c2", 22),
              }));
  EXPECT_THAT(metricNamesAndValues(gauges),
              UnorderedElementsAreArray({
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.g1", 13),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.g2", 14),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.healthy", 1),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.g1", 23),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.g2", 24),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.healthy", 1),
              }));
}

TEST_F(PerEndpointMetricsTest, Tags) {
  auto& cluster = makeCluster("cluster1", 0);
  auto& host1 = addHost(cluster);
  std::string hostname = "host.example.com";
  EXPECT_CALL(host1, hostname()).WillOnce(ReturnRef(hostname));
  addHost(cluster);

  auto [counters, gauges] = run();

  // Only the first host has a hostname, so only it has that tag.
  EXPECT_THAT(getMetric("cluster.cluster1.endpoint.127.0.0.1_80.c1", counters).tags(),
              UnorderedElementsAreArray({
                  Stats::Tag{"envoy.cluster_name", "cluster1"},
                  Stats::Tag{"envoy.endpoint_address", "127.0.0.1:80"},
                  Stats::Tag{"envoy.endpoint_hostname", hostname},
              }));

  EXPECT_THAT(getMetric("cluster.cluster1.endpoint.127.0.0.2_80.c1", counters).tags(),
              UnorderedElementsAreArray({
                  Stats::Tag{"envoy.cluster_name", "cluster1"},
                  Stats::Tag{"envoy.endpoint_address", "127.0.0.2:80"},
              }));
}

TEST_F(PerEndpointMetricsTest, FixedTags) {
  auto& cluster = makeCluster("cluster1", 1);
  Stats::TagVector fixed_tags{{"fixed1", "value1"}, {"fixed2", "value2"}};
  EXPECT_CALL(cluster.info_->stats_store_, fixedTags()).WillOnce(ReturnRef(fixed_tags));

  auto [counters, gauges] = run();

  EXPECT_THAT(getMetric("cluster.cluster1.endpoint.127.0.0.1_80.c1", counters).tags(),
              UnorderedElementsAreArray({
                  Stats::Tag{"envoy.cluster_name", "cluster1"},
                  Stats::Tag{"envoy.endpoint_address", "127.0.0.1:80"},
                  Stats::Tag{"fixed1", "value1"},
                  Stats::Tag{"fixed2", "value2"},
              }));
}

// Only clusters with the setting enabled produce metrics.
TEST_F(PerEndpointMetricsTest, Enabled) {
  auto& disabled = makeCluster("disabled", 1);
  auto& enabled = makeCluster("enabled", 1);
  EXPECT_CALL(*disabled.info_, perEndpointStats()).WillOnce(Return(false));
  EXPECT_CALL(*enabled.info_, perEndpointStats()).WillOnce(Return(true));

  auto [counters, gauges] = run();

  EXPECT_THAT(metricNames(counters),
              UnorderedElementsAreArray({"cluster.enabled.endpoint.127.0.0.2_80.c1",
                                         "cluster.enabled.endpoint.127.0.0.2_80.c2"}));
  EXPECT_THAT(metricNames(gauges),
              UnorderedElementsAreArray({"cluster.enabled.endpoint.127.0.0.2_80.g1",
                                         "cluster.enabled.endpoint.127.0.0.2_80.g2",
                                         "cluster.enabled.endpoint.127.0.0.2_80.healthy"}));
}

// Stats use observability name, and are sanitized.
TEST_F(PerEndpointMetricsTest, SanitizedObservabilityName) {
  auto& cluster = makeCluster("notthisname", 1);
  std::string name = "observability:name";
  EXPECT_CALL(*cluster.info_, observabilityName()).WillOnce(ReturnRef(name));

  auto [counters, gauges] = run();

  EXPECT_THAT(metricNames(counters),
              UnorderedElementsAreArray({"cluster.observability_name.endpoint.127.0.0.1_80.c1",
                                         "cluster.observability_name.endpoint.127.0.0.1_80.c2"}));
  EXPECT_THAT(
      metricNames(gauges),
      UnorderedElementsAreArray({"cluster.observability_name.endpoint.127.0.0.1_80.g1",
                                 "cluster.observability_name.endpoint.127.0.0.1_80.g2",
                                 "cluster.observability_name.endpoint.127.0.0.1_80.healthy"}));

  EXPECT_THAT(getMetric("cluster.observability_name.endpoint.127.0.0.1_80.c1", counters).tags(),
              Contains(Stats::Tag{"envoy.cluster_name", "observability_name"}));
}

} // namespace
} // namespace Upstream
} // namespace Envoy
