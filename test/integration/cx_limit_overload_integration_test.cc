#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"

#include "test/integration/integration.h"
#include "test/test_common/test_runtime.h"

namespace Envoy {

namespace {

envoy::config::overload::v3::OverloadManager overloadManagerProtoConfig(uint32_t max_cx) {
  const static std::string yaml_config = R"EOF(
          resource_monitors:
            - name: "envoy.resource_monitors.global_downstream_max_connections"
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.resource_monitors.downstream_connections.v3.DownstreamConnectionsConfig
                max_active_downstream_connections: {}
        )EOF";
  return TestUtility::parseYaml<envoy::config::overload::v3::OverloadManager>(
      fmt::format(yaml_config, std::to_string(max_cx)));
}

class GlobalDownstreamCxLimitIntegrationTest : public testing::Test, public BaseIntegrationTest {
protected:
  GlobalDownstreamCxLimitIntegrationTest()
      : BaseIntegrationTest(Network::Address::IpVersion::v4, ConfigHelper::tcpProxyConfig()) {}

  void initializeOverloadManager(uint32_t max_cx) {
    overload_manager_config_ = overloadManagerProtoConfig(max_cx);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      *bootstrap.mutable_overload_manager() = this->overload_manager_config_;
    });
    initialize();
  }

  AssertionResult waitForConnections(uint32_t expected_connections) {
    absl::Notification num_downstream_conns_reached;
    absl::Mutex lock;
    test_server_->server().dispatcher().post([this, &lock, &num_downstream_conns_reached,
                                              expected_connections]() {
      auto& overload_state = test_server_->server().overloadManager().getThreadLocalOverloadState();
      const auto& monitor = overload_state.getProactiveResourceMonitorForTest(
          Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections);
      ASSERT_TRUE(monitor.has_value());
      absl::MutexLock l(&lock);
      const auto reached_expected_count =
          [&monitor, expected_connections]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock) {
            return monitor->currentResourceUsage() == expected_connections;
          };
      if (timeSystem().waitFor(lock, absl::Condition(&reached_expected_count),
                               std::chrono::milliseconds(5000))) {
        num_downstream_conns_reached.Notify();
      };
    });
    return AssertionSuccess();
  }

private:
  envoy::config::overload::v3::OverloadManager overload_manager_config_;
};

TEST_F(GlobalDownstreamCxLimitIntegrationTest, GlobalLimitInOverloadManager) {
  initializeOverloadManager(6);
  std::vector<IntegrationTcpClientPtr> tcp_clients;
  std::vector<FakeRawConnectionPtr> raw_conns;
  for (int i = 0; i <= 5; ++i) {
    tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
    raw_conns.emplace_back();
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(raw_conns.back()));
    ASSERT_TRUE(tcp_clients.back()->connected());
  }
  test_server_->waitForCounterEq("listener.127.0.0.1_0.downstream_global_cx_overflow", 0);
  // 7th connection should fail because we have hit the configured limit for
  // `max_active_downstream_connections`.
  tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
  raw_conns.emplace_back();
  ASSERT_FALSE(
      fake_upstreams_[0]->waitForRawConnection(raw_conns.back(), std::chrono::milliseconds(500)));
  tcp_clients.back()->waitForDisconnect();
  test_server_->waitForCounterEq("listener.127.0.0.1_0.downstream_global_cx_overflow", 1);
  // Get rid of the client that failed to connect.
  tcp_clients.back()->close();
  tcp_clients.pop_back();
  raw_conns.pop_back();
  // Get rid of the first successfully connected client to be able to establish new connection.
  tcp_clients.front()->close();
  ASSERT_TRUE(raw_conns.front()->waitForDisconnect());
  ASSERT_TRUE(waitForConnections(5));
  // As 6th client disconnected, we can again establish a connection.
  tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
  raw_conns.emplace_back();
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(raw_conns.back()));
  ASSERT_TRUE(tcp_clients.back()->connected());
  for (auto& tcp_client : tcp_clients) {
    tcp_client->close();
  }
  tcp_clients.clear();
  raw_conns.clear();
}

TEST_F(GlobalDownstreamCxLimitIntegrationTest, GlobalLimitSetViaRuntimeKeyAndOverloadManager) {
  // Configure global connections limit via deprecated runtime key.
  config_helper_.addRuntimeOverride("overload.global_downstream_max_connections", "3");
  initializeOverloadManager(2);
  const std::string log_line =
      "Global downstream connections limits is configured via deprecated runtime key "
      "overload.global_downstream_max_connections and in "
      "envoy.resource_monitors.global_downstream_max_connections. Using overload manager config.";
  std::vector<IntegrationTcpClientPtr> tcp_clients;
  std::vector<FakeRawConnectionPtr> raw_conns;
  std::function<void()> establish_conns = [this, &tcp_clients, &raw_conns]() {
    for (int i = 0; i < 2; ++i) {
      tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
      raw_conns.emplace_back();
      ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(raw_conns.back()));
      ASSERT_TRUE(tcp_clients.back()->connected());
    }
  };
  EXPECT_LOG_CONTAINS_N_TIMES("warn", log_line, 1, { establish_conns(); });
  // Third connection should fail because we have hit the configured limit in overload manager.
  tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
  raw_conns.emplace_back();
  ASSERT_FALSE(
      fake_upstreams_[0]->waitForRawConnection(raw_conns.back(), std::chrono::milliseconds(500)));
  tcp_clients.back()->waitForDisconnect();
  test_server_->waitForCounterEq("listener.127.0.0.1_0.downstream_global_cx_overflow", 1);
  for (auto& tcp_client : tcp_clients) {
    tcp_client->close();
  }
  tcp_clients.clear();
  raw_conns.clear();
}

TEST_F(GlobalDownstreamCxLimitIntegrationTest, GlobalLimitOptOutRespected) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    listener->set_ignore_global_conn_limit(true);
  });
  initializeOverloadManager(2);
  std::vector<IntegrationTcpClientPtr> tcp_clients;
  std::vector<FakeRawConnectionPtr> raw_conns;
  // All clients succeed to connect due to global conn limit opt out set.
  for (int i = 0; i <= 5; ++i) {
    tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
    raw_conns.emplace_back();
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(raw_conns.back()));
    ASSERT_TRUE(tcp_clients.back()->connected());
  }
  test_server_->waitForCounterEq("listener.127.0.0.1_0.downstream_global_cx_overflow", 0);
  for (auto& tcp_client : tcp_clients) {
    tcp_client->close();
  }
  tcp_clients.clear();
  raw_conns.clear();
}

TEST_F(GlobalDownstreamCxLimitIntegrationTest, PerListenerLimitAndGlobalLimitInOverloadManager) {
  config_helper_.addRuntimeOverride("envoy.resource_limits.listener.listener_0.connection_limit",
                                    "2");
  initializeOverloadManager(5);
  std::vector<IntegrationTcpClientPtr> tcp_clients;
  std::vector<FakeRawConnectionPtr> raw_conns;
  for (int i = 0; i < 2; ++i) {
    tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
    raw_conns.emplace_back();
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(raw_conns.back()));
    ASSERT_TRUE(tcp_clients.back()->connected());
  }
  // Third connection should fail because we have hit the configured limit per listener.
  tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
  raw_conns.emplace_back();
  ASSERT_FALSE(
      fake_upstreams_[0]->waitForRawConnection(raw_conns.back(), std::chrono::milliseconds(500)));
  tcp_clients.back()->waitForDisconnect();
  test_server_->waitForCounterEq("listener.127.0.0.1_0.downstream_cx_overflow", 1);
  test_server_->waitForCounterEq("listener.127.0.0.1_0.downstream_global_cx_overflow", 0);
  for (auto& tcp_client : tcp_clients) {
    tcp_client->close();
  }
  tcp_clients.clear();
  raw_conns.clear();
}

} // namespace
} // namespace Envoy
