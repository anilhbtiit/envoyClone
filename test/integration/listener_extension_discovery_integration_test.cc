#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/extension/v3/config_discovery.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/filters/test_listener_filter.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "test/integration/filters/test_listener_filter.h"
#include "test/integration/integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class ExtensionDiscoveryIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                          public BaseIntegrationTest {
public:
  ExtensionDiscoveryIntegrationTest()
      : BaseIntegrationTest(ipVersion(), ConfigHelper::baseConfig() + R"EOF(
    filter_chains:
    - filters:
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp_stats
          cluster: cluster_0
)EOF") {}

  void addDynamicFilter(const std::string& name, bool apply_without_warming,
                        bool set_default_config = true, bool rate_limit = false) {
    config_helper_.addConfigModifier([&]( envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener_filter = bootstrap.mutable_static_resources()->mutable_listeners(0)->add_listener_filters();
      listener_filter->set_name(name);

      auto* discovery = listener_filter->mutable_config_discovery();
      discovery->add_type_urls(
              "type.googleapis.com/test.integration.filters.TestTcpListenerFilterConfig");
      if (set_default_config) {
        auto default_configuration = test::integration::filters::TestTcpListenerFilterConfig();
        default_configuration.set_drain_bytes(kDefaultDrainBytes);
        discovery->mutable_default_config()->PackFrom(default_configuration);
      }

      discovery->set_apply_default_config_without_warming(apply_without_warming);
      discovery->mutable_config_source()->set_resource_api_version(
              envoy::config::core::v3::ApiVersion::V3);
      auto* api_config_source = discovery->mutable_config_source()->mutable_api_config_source();
      api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
      api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
      if (rate_limit) {
        api_config_source->mutable_rate_limit_settings()->mutable_max_tokens()->set_value(10);
      }
      auto* grpc_service = api_config_source->add_grpc_services();
      setGrpcService(*grpc_service, "ecds_cluster", getEcdsFakeUpstream().localAddress());
    });

  }

  void initialize() override {
    defer_listener_finalization_ = true;
    setUpstreamCount(1);

    // Add an xDS cluster for extension config discovery.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* ecds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ecds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ecds_cluster->set_name("ecds_cluster");
      ConfigHelper::setHttp2(*ecds_cluster);
    });
    BaseIntegrationTest::initialize();
    registerTestServerPorts({port_name_});
  }

  ~ExtensionDiscoveryIntegrationTest() override {
    if (ecds_connection_ != nullptr) {
      AssertionResult result = ecds_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = ecds_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      ecds_connection_.reset();
    }
  }

  void createUpstreams() override {
    BaseIntegrationTest::createUpstreams();
    // Create the extension config discovery upstream (fake_upstreams_[1]).
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void waitXdsStream() {
    // Wait for ECDS stream.
    auto& ecds_upstream = getEcdsFakeUpstream();
    AssertionResult result = ecds_upstream.waitForHttpConnection(*dispatcher_, ecds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = ecds_connection_->waitForNewStream(*dispatcher_, ecds_stream_);
    RELEASE_ASSERT(result, result.message());
    ecds_stream_->startGrpcStream();
  }

  void sendXdsResponse(const std::string& version, const uint32_t drain_bytes,  bool ttl = false) {
    // The to-be-drained bytes has to be smaller than data size.
    ASSERT(drain_bytes <= data_.size());

    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url("type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig");
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(filter_name_);
    envoy::service::discovery::v3::Resource resource;
    resource.set_name(filter_name_);

    auto configuration = test::integration::filters::TestTcpListenerFilterConfig();
    configuration.set_drain_bytes(drain_bytes);
    typed_config.mutable_typed_config()->PackFrom(configuration);
    resource.mutable_resource()->PackFrom(typed_config);
    if (ttl) {
      resource.mutable_ttl()->set_seconds(1);
    }
    response.add_resources()->PackFrom(resource);
    ecds_stream_->sendGrpcMessage(response);
  }

  // Client sends data_, which is drained by Envoy listener filter based on config, then received by upstream.
  void sendDataVerifyResults (uint32_t drain_bytes) {
    test_server_->waitUntilListenersReady();
    EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort(port_name_));
    ASSERT_TRUE(tcp_client->write(data_));
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
    std::string received_data;
    ASSERT_TRUE(fake_upstream_connection->waitForData(data_.size() - drain_bytes, &received_data));
    const std::string expected_data = data_.substr(drain_bytes, std::string::npos);
    EXPECT_EQ(expected_data, received_data);
    tcp_client->close();
  }

  const uint32_t    kDefaultDrainBytes = 2;
  const std::string filter_name_ = "foo";
  const std::string data_ = "HelloWorld";
  const std::string port_name_ = "http";

  FakeUpstream& getEcdsFakeUpstream() const { return *fake_upstreams_[1]; }

  // gRPC ECDS set-up
  FakeHttpConnectionPtr ecds_connection_{nullptr};
  FakeStreamPtr ecds_stream_{nullptr};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, ExtensionDiscoveryIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(ExtensionDiscoveryIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update to have listener filter drain 5 bytes of data.
  sendXdsResponse("1", 5);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter."+ filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(5);

  // Send 2nd config update to have listener filter drain 3 bytes of data.
  sendXdsResponse("2", 3);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter."+ filter_name_ + ".config_reload", 2);
  sendDataVerifyResults(3);
}

TEST_P(ExtensionDiscoveryIntegrationTest, BasicSuccessWithTtl) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false, false);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update with TTL 1s, and have listener filter drain 5 bytes of data.
  sendXdsResponse("1", 5, true);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter."+ filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(5);

  // Wait for configuration expired. Then start a TCP connection.
  // The missing config listener filter will be installed to handle the connection.
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter."+ filter_name_ + ".config_reload", 2);
  EXPECT_LOG_CONTAINS("warn", "Close socket and stop the iteration onAccept.",
                      {
                        IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort(port_name_));
                        auto result = tcp_client->write(data_);
                        if (result) {
                          tcp_client->waitForDisconnect();
                        }
                      });
}

TEST_P(ExtensionDiscoveryIntegrationTest, BasicSuccessWithTtlWithDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false, true);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update with TTL 1s, and have listener filter drain 5 bytes of data.
  sendXdsResponse("1", 5, true);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter."+ filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(5);

  // Wait for configuration expired. The default filter will be installed.
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter."+ filter_name_ + ".config_reload", 2);
  // Start a TCP connection. The default filter drain 2 bytes.
  sendDataVerifyResults(kDefaultDrainBytes);
}


// This one TBD
TEST_P(ExtensionDiscoveryIntegrationTest, BasicFailWithDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false, true);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update with invalid config (drain_bytes has to >=2).
  sendXdsResponse("1", 1);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter."+ filter_name_ + ".config_fail", 1);
  // The default filter will be installed. Start a TCP connection. The default filter drain 2 bytes.
  sendDataVerifyResults(kDefaultDrainBytes);
}

// This one TBD
TEST_P(ExtensionDiscoveryIntegrationTest, BasicFailWithoutDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false, false);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update with invalid config (drain_bytes has to >=2).
  sendXdsResponse("1", 1);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter."+ filter_name_ + ".config_fail", 1);
  // The missing config filter will be installed when a correction is created.
  // The missing config filter will close the connection.
  EXPECT_LOG_CONTAINS("warn", "Close socket and stop the iteration onAccept.",
                      {
                        IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort(port_name_));
                        auto result = tcp_client->write(data_);
                        if (result) {
                          tcp_client->waitForDisconnect();
                        }
                      });
}


TEST_P(ExtensionDiscoveryIntegrationTest, BasicWithoutWarming) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  initialize();

  // Send data without send config update.
  sendDataVerifyResults(kDefaultDrainBytes);
  // Send update should cause a different response.
  sendXdsResponse("1", 3);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter."+ filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(3);
}

TEST_P(ExtensionDiscoveryIntegrationTest, BasicWithoutWarmingFail) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  initialize();

  sendXdsResponse("1", 1);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter."+ filter_name_ + ".config_fail", 1);
  sendDataVerifyResults(kDefaultDrainBytes);
}

TEST_P(ExtensionDiscoveryIntegrationTest, BasicTwoSubscriptionsSameNameWithoutWarming) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  // Adding a filter with same name overrides the previous one.
  addDynamicFilter(filter_name_, false);
  initialize();

  sendXdsResponse("1", 3);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter."+ filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(3);
}

TEST_P(ExtensionDiscoveryIntegrationTest, BasicTwoSubscriptionsSameNameWithWarming) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false);
  // Adding a filter with same name overrides the previous one.
  addDynamicFilter(filter_name_, true);
  initialize();

  sendXdsResponse("1", 3);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter."+ filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(3);
}

TEST_P(ExtensionDiscoveryIntegrationTest, DestroyDuringInit) {
  // If rate limiting is enabled on the config source, gRPC mux drainage updates the requests
  // queue size on destruction. The update calls out to stats scope nested under the extension
  // config subscription stats scope. This test verifies that the stats scope outlasts the gRPC
  // subscription.
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", false, true);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  test_server_.reset();
  auto result = ecds_connection_->waitForDisconnect();
  RELEASE_ASSERT(result, result.message());
  ecds_connection_.reset();
}

} // namespace
} // namespace Envoy
