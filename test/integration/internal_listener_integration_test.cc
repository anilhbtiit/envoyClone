#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"

#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/network/connection.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/api_version.h"

#include "test/common/grpc/grpc_client_integration.h"

#include "test/integration/base_integration_test.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

class InternalListenerIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                        public BaseIntegrationTest {
public:
  InternalListenerIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfig() + +R"EOF(
    filter_chains:
    - filters:
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp_stats
          cluster: cluster_0
)EOF") {}

  void initialize() override {
    config_helper_.renameListener("tcp");
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto& listener = *bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener.mutable_address()->mutable_envoy_internal_address()->set_server_listener_name(
          "internal_listener");
      listener.mutable_internal_listener();
    });
    BaseIntegrationTest::initialize();
  }
};

TEST_P(InternalListenerIntegrationTest, Basic) {
  initialize();
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());

  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        (*(*listener->mutable_metadata()->mutable_filter_metadata())["random_filter_name"]
              .mutable_fields())["random_key"]
            .set_number_value(1);
      });

  new_config_helper.setLds("1");

  test_server_->waitForCounterEq("listener_manager.listener_modified", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_draining", 0);
}

TEST_P(InternalListenerIntegrationTest, InplaceUpdate) {
  initialize();
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());

  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        auto new_filter_chain = *listener->mutable_filter_chains(0);
        listener->mutable_filter_chains()->Add()->MergeFrom(new_filter_chain);
        *(listener->mutable_filter_chains(1)
              ->mutable_filter_chain_match()
              ->mutable_application_protocols()
              ->Add()) = "alpn";
      });

  new_config_helper.setLds("1");

  test_server_->waitForCounterEq("listener_manager.listener_modified", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_draining", 0);
}

TEST_P(InternalListenerIntegrationTest, DeleteListener) {
  initialize();
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());

  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        bootstrap.mutable_static_resources()->mutable_listeners()->RemoveLast();
      });

  new_config_helper.setLds("1");

  test_server_->waitForCounterEq("listener_manager.listener_removed", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_draining", 0);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, InternalListenerIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace
} // namespace Envoy
