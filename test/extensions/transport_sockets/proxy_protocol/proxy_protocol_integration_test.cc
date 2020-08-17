#include "test/integration/integration.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/extensions/transport_sockets/proxy_protocol/v3/upstream_proxy_protocol.pb.h"

namespace Envoy {
namespace {

class ProxyProtocolIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public BaseIntegrationTest {
public:
  ProxyProtocolIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {}

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void setVersion(envoy::config::core::v3::ProxyProtocolConfig_Version version) { version_ = version; }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* transport_socket = bootstrap.mutable_static_resources()
                                   ->mutable_clusters(0)
                                   ->mutable_transport_socket();
      transport_socket->set_name("envoy.transport_sockets.upstream_proxy_protocol");
      envoy::config::core::v3::TransportSocket raw_transport_socket;
      raw_transport_socket.set_name("envoy.transport_sockets.raw_buffer");
      envoy::config::core::v3::ProxyProtocolConfig proxy_proto_config;
      proxy_proto_config.set_version(version_);
      envoy::extensions::transport_sockets::proxy_protocol::v3::ProxyProtocolUpstreamTransport proxy_proto_transport;
      proxy_proto_transport.mutable_transport_socket()->MergeFrom(raw_transport_socket);
      proxy_proto_transport.mutable_config()->MergeFrom(proxy_proto_config);
      transport_socket->mutable_typed_config()->PackFrom(proxy_proto_transport);
    });
    BaseIntegrationTest::initialize();
  }

private:
  envoy::config::core::v3::ProxyProtocolConfig_Version version_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtocolIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test sending proxy protocol v1
TEST_P(ProxyProtocolIntegrationTest, TestV1ProxyProtocol) {
  setVersion(envoy::config::core::v3::ProxyProtocolConfig::V1);
  initialize();

  auto listener_port = lookupPort("listener_0");
  auto tcp_client = makeTcpConnection(listener_port);
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  std::string observed_data;
  ASSERT_TRUE(tcp_client->write("data"));
  if (GetParam() == Network::Address::IpVersion::v4) {
    ASSERT_TRUE(fake_upstream_connection->waitForData(48, &observed_data));
    std::ostringstream stream;
    stream << "PROXY TCP4 127\\.0\\.0\\.1 127\\.0\\.0\\.1 [0-9]{1,5} " << listener_port
           << "\r\ndata";
    EXPECT_THAT(observed_data, testing::MatchesRegex(stream.str()));
  } else if (GetParam() == Network::Address::IpVersion::v6) {
    ASSERT_TRUE(fake_upstream_connection->waitForData(36, &observed_data));
    std::ostringstream stream;
    stream << "PROXY TCP6 ::1 ::1 [0-9]{1,5} " << listener_port << "\r\ndata";
    EXPECT_THAT(observed_data, testing::MatchesRegex(stream.str()));
  }

  auto previous_data = observed_data;
  observed_data.clear();
  ASSERT_TRUE(tcp_client->write(" more data"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(previous_data.length() + 10, &observed_data));
  EXPECT_EQ(previous_data + " more data", observed_data);

  tcp_client->close();
  auto _ = fake_upstream_connection->waitForDisconnect();
}

// Test sending proxy protocol v2
TEST_P(ProxyProtocolIntegrationTest, TestV2ProxyProtocol) {
  setVersion(envoy::config::core::v3::ProxyProtocolConfig::V2);
  initialize();

  auto listener_port = lookupPort("listener_0");
  auto tcp_client = makeTcpConnection(listener_port);
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  std::string observed_data;
  ASSERT_TRUE(tcp_client->write("data"));
  if (GetParam() == Envoy::Network::Address::IpVersion::v4) {
    ASSERT_TRUE(fake_upstream_connection->waitForData(32, &observed_data));
    // - signature
    // - version and command type, address family and protocol, length of addresses
    // - src address, dest address
    auto header_start = "\x0d\x0a\x0d\x0a\x00\x0d\x0a\x51\x55\x49\x54\x0a\
                         \x21\x11\x00\x0c\
                         \x7f\x00\x00\x01\x7f\x00\x00\x01";
    EXPECT_THAT(observed_data, testing::StartsWith(header_start));
    EXPECT_EQ(static_cast<uint8_t>(observed_data[26]), listener_port >> 8);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[27]), listener_port & 0xFF);
    EXPECT_THAT(observed_data, testing::EndsWith("data"));
  } else if (GetParam() == Envoy::Network::Address::IpVersion::v6) {
    ASSERT_TRUE(fake_upstream_connection->waitForData(56, &observed_data));
    // - signature
    // - version and command type, address family and protocol, length of addresses
    // - src address
    // - dest address
    auto header_start = "\x0d\x0a\x0d\x0a\x00\x0d\x0a\x51\x55\x49\x54\x0a\
                         \x21\x21\x00\x24\
                         \x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\
                         \x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01";
    EXPECT_THAT(observed_data, testing::StartsWith(header_start));
    EXPECT_EQ(static_cast<uint8_t>(observed_data[50]), listener_port >> 8);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[51]), listener_port & 0xFF);
    EXPECT_THAT(observed_data, testing::EndsWith("data"));
  }

  auto previous_data = observed_data;
  observed_data.clear();
  ASSERT_TRUE(tcp_client->write(" more data"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(previous_data.length() + 10, &observed_data));
  EXPECT_EQ(previous_data + " more data", observed_data);

  tcp_client->close();
  auto _ = fake_upstream_connection->waitForDisconnect();
}

}
} // namespace Envoy