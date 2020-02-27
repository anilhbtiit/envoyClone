#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

using testing::HasSubstr;

// This is a minimal litmus test for the v2 xDS APIs.
class XdsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                           public HttpIntegrationTest {
public:
  XdsIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
  }

  void createEnvoy() override {
    createEnvoyServer({
        "test/config/integration/server_xds.bootstrap.yaml",
        "test/config/integration/server_xds.cds.yaml",
        "test/config/integration/server_xds.eds.yaml",
        "test/config/integration/server_xds.lds.yaml",
        "test/config/integration/server_xds.rds.yaml",
    });
  }

  void createEnvoyServer(const ApiFilesystemConfig& api_filesystem_config) {
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    createApiTestServer(api_filesystem_config, {"http"}, false, false, false);
    EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());
    EXPECT_EQ(1, test_server_->counter("http.router.rds.route_config_0.update_success")->value());
    EXPECT_EQ(1, test_server_->counter("cluster_manager.cds.update_success")->value());
    EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.update_success")->value());
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, XdsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(XdsIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

class XdsIntegrationTestTypedStruct : public XdsIntegrationTest {
public:
  XdsIntegrationTestTypedStruct() = default;

  void createEnvoy() override {
    createEnvoyServer({
        "test/config/integration/server_xds.bootstrap.yaml",
        "test/config/integration/server_xds.cds.yaml",
        "test/config/integration/server_xds.eds.yaml",
        "test/config/integration/server_xds.lds.typed_struct.yaml",
        "test/config/integration/server_xds.rds.yaml",
    });
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, XdsIntegrationTestTypedStruct,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(XdsIntegrationTestTypedStruct, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

using LdsIntegrationTest = HttpProtocolIntegrationTest;

INSTANTIATE_TEST_SUITE_P(Protocols, LdsIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP1}, {FakeHttpConnection::Type::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Sample test making sure our config framework correctly reloads listeners.
TEST_P(LdsIntegrationTest, ReloadConfig) {
  autonomous_upstream_ = true;
  initialize();
  // Given we're using LDS in this test, initialize() will not complete until
  // the initial LDS file has loaded.
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());

  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  // HTTP 1.0 is disabled by default.
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response, true);
  EXPECT_TRUE(response.find("HTTP/1.1 426 Upgrade Required\r\n") == 0);

  // Create a new config with HTTP/1.0 proxying.
  ConfigHelper new_config_helper(version_, *api_,
                                 MessageUtil::getJsonStringFromMessage(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        hcm.mutable_http_protocol_options()->set_accept_http_10(true);
        hcm.mutable_http_protocol_options()->set_default_host_for_http_10("default.com");
      });

  // Create an LDS response with the new config, and reload config.
  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 2);

  // HTTP 1.0 should now be enabled.
  std::string response2;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response2, false);
  EXPECT_THAT(response2, HasSubstr("HTTP/1.0 200 OK\r\n"));
}

// TODO(lambdai): skip the tests in ipv6 env since filter chain matches varies at ipv4 dst address.
// This test case confirms a new listener config with additional filter chains doesn't impact the
// existing filter chain and connection.
TEST_P(LdsIntegrationTest, ReloadConfigAddingFilterChain) {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        hcm.mutable_http_protocol_options()->set_accept_http_10(true);
        hcm.mutable_http_protocol_options()->set_default_host_for_http_10("default.com");
      });
  // dst ip = all
  initialize();
  // Given we're using LDS in this test, initialize() will not complete until
  // the initial LDS file has loaded.
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());

  // step1: verify the first (and the only) filter chain is working correctly
  std::string response;
  auto conn1 = sendRawHttpAndWaitForHeader(1, lookupPort("http"),
                                           "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", &response,
                                           /*disconnect_after_headers_complete=*/false);
  EXPECT_THAT(response, HasSubstr("HTTP/1.1 200 OK\r\n"));

  ConfigHelper new_config_helper(version_, *api_,
                                 MessageUtil::getJsonStringFromMessage(config_helper_.bootstrap()));
  // added filter chain: dst ip = 127.0.0.2
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        auto* standard_filter_chain = listener->mutable_filter_chains(0);
        auto* add_filter_chain = listener->add_filter_chains();
        add_filter_chain->CopyFrom(*standard_filter_chain);
        add_filter_chain->set_name("127.0.0.2");
        auto* src_ip = add_filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges();
        src_ip->set_address_prefix("127.0.0.2");
        src_ip->mutable_prefix_len()->set_value(32);
      });

  // Create an LDS response with the new config, and reload config.
  new_config_helper.setLds("1");

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 2);

  // step 2: verify the new filter chain is adopted
  std::string response2;
  auto conn2 = sendRawHttpAndWaitForHeader(2, lookupPort("http"),
                                           "GET / HTTP/1.1\r\nHost: 127.0.0.2\r\n\r\n", &response2,
                                           /*disconnect_after_headers_complete=*/false);
  EXPECT_THAT(response2, HasSubstr("HTTP/1.1 200 OK\r\n"));

  // step3: verify the opened connection on first filter chain is not impacted by the listener
  // update.
  conn1->clearShouldExit();
  response.clear();
  conn1->write("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
  conn1->runUntil();
  EXPECT_THAT(response, HasSubstr("HTTP/1.1 200 OK\r\n"));

  conn1->close();
  conn2->close();
}

// Confirm that a new listener config with one fewer filter chain will drain the connections on that
// filter chain.
TEST_P(LdsIntegrationTest, ReloadConfigDeletingFilterChain) {
  autonomous_upstream_ = true;

  // 2 filter chains in initial listener
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* standard_filter_chain = listener->mutable_filter_chains(0);
    auto* add_filter_chain = listener->add_filter_chains();
    add_filter_chain->CopyFrom(*standard_filter_chain);
    add_filter_chain->set_name("127.0.0.2");
    auto* dst_ip = add_filter_chain->mutable_filter_chain_match()->add_prefix_ranges();
    dst_ip->set_address_prefix("127.0.0.2");
    dst_ip->mutable_prefix_len()->set_value(32);
  });

  initialize();
  // Given we're using LDS in this test, initialize() will not complete until
  // the initial LDS file has loaded.
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());

  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response,
                                /*disconnect_after_headers_complete=*/true);
  EXPECT_TRUE(response.find("HTTP/1.1 426 Upgrade Required\r\n") == 0);

  ConfigHelper new_config_helper(version_, *api_,
                                 MessageUtil::getJsonStringFromMessage(config_helper_.bootstrap()));
  // delete the filter chain matching dst ip = 127.0.0.2
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->mutable_filter_chains()->RemoveLast();
      });

  // Create an LDS response with the new config, and reload config.
  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 2);

  std::string response2;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response2, true);
  EXPECT_TRUE(response2.find("HTTP/1.1 426 Upgrade Required\r\n") == 0);
  ASSERT("connection on filter chain 0 is alive after timeout");
}

// Confirm that a new listener config with updated filter chain will drain the connection on the
// existing filter chain, and new connections will see the updated filter chain.
TEST_P(LdsIntegrationTest, ReloadConfigUpdatingFilterChain) {}

// Sample test making sure our config framework informs on listener failure.
TEST_P(LdsIntegrationTest, FailConfigLoad) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    filter_chain->mutable_filters(0)->clear_typed_config();
    filter_chain->mutable_filters(0)->set_name("grewgragra");
  });
  EXPECT_DEATH_LOG_TO_STDERR(initialize(),
                             "Didn't find a registered implementation for name: 'grewgragra'");
}

} // namespace
} // namespace Envoy
