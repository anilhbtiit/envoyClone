#include "envoy/extensions/filters/network/connection_limit/v3/connection_limit.pb.h"
#include "envoy/extensions/filters/network/connection_limit/v3/connection_limit.pb.validate.h"

#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/network/connection_limit/connection_limit.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ConnectionLimitFilter {

class ConnectionLimitTestBase : public testing::Test {
public:
  void initialize(const std::string& filter_yaml) {
    envoy::extensions::filters::network::connection_limit::v3::ConnectionLimit proto_config;
    TestUtility::loadFromYamlAndValidate(filter_yaml, proto_config);
    config_ = std::make_shared<Config>(proto_config, stats_store_, runtime_);
  }

  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  ConfigSharedPtr config_;
};

class ConnectionLimitFilterTest : public ConnectionLimitTestBase {
public:
  struct ActiveFilter {
    ActiveFilter(const ConfigSharedPtr& config) : filter_(config) {
      filter_.initializeReadFilterCallbacks(read_filter_callbacks_);
    }

    NiceMock<Network::MockReadFilterCallbacks> read_filter_callbacks_;
    Filter filter_;
  };
};

// Basic no connection limit case.
TEST_F(ConnectionLimitFilterTest, NoConnectionLimit) {
  initialize(R"EOF(
stat_prefix: connection_limit_stats
max_connections: 1
delay: 0.2s
)EOF");

  InSequence s;
  ActiveFilter active_filter(config_);
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter.filter_.onNewConnection());
  EXPECT_EQ(1, TestUtility::findGauge(stats_store_,
                                      "connection_limit.connection_limit_stats.active_connections")
                   ->value());
  EXPECT_EQ(0, TestUtility::findCounter(
                   stats_store_, "connection_limit.connection_limit_stats.limited_connections")
                   ->value());
}

// Basic connection limit case.
TEST_F(ConnectionLimitFilterTest, ConnectionLimit) {
  initialize(R"EOF(
stat_prefix: connection_limit_stats
max_connections: 2
delay: 0s
)EOF");

  // First connection is OK.
  InSequence s;
  ActiveFilter active_filter1(config_);
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter1.filter_.onNewConnection());

  // Second connection is OK.
  ActiveFilter active_filter2(config_);
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter2.filter_.onNewConnection());
  EXPECT_EQ(2, TestUtility::findGauge(stats_store_,
                                      "connection_limit.connection_limit_stats.active_connections")
                   ->value());

  // Third connection should be connection limited.
  ActiveFilter active_filter3(config_);
  EXPECT_CALL(active_filter3.read_filter_callbacks_.connection_, close(_));
  EXPECT_EQ(Network::FilterStatus::StopIteration, active_filter3.filter_.onNewConnection());
  EXPECT_EQ(1, TestUtility::findCounter(
                   stats_store_, "connection_limit.connection_limit_stats.limited_connections")
                   ->value());
  EXPECT_EQ(2, TestUtility::findGauge(stats_store_,
                                      "connection_limit.connection_limit_stats.active_connections")
                   ->value());
}

// Verify the runtime disable functionality.
TEST_F(ConnectionLimitFilterTest, RuntimeDisabled) {
  initialize(R"EOF(
stat_prefix: connection_limit_stats
max_connections: 1
delay: 0.2s
runtime_enabled:
  default_value: true
  runtime_key: foo_key
)EOF");

  // First connection is OK.
  InSequence s;
  ActiveFilter active_filter1(config_);
  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo_key", true)).WillOnce(Return(true));
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter1.filter_.onNewConnection());

  // Second connection should be connection limited but won't be due to filter disable.
  ActiveFilter active_filter2(config_);
  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo_key", true)).WillOnce(Return(false));
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter2.filter_.onNewConnection());
  EXPECT_EQ(1, TestUtility::findGauge(stats_store_,
                                      "connection_limit.connection_limit_stats.active_connections")
                   ->value());
  EXPECT_EQ(0, TestUtility::findCounter(
                   stats_store_, "connection_limit.connection_limit_stats.limited_connections")
                   ->value());
}

} // namespace ConnectionLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
