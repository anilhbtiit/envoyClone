#include "ssl_integration_test.h"

#include <memory>
#include <string>

#include "envoy/config/transport_socket/capture/v2/capture.pb.h"
#include "envoy/extensions/transport_socket/capture/v2alpha/capture.pb.h"

#include "common/event/dispatcher_impl.h"
#include "common/network/utility.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_manager_impl.h"

#include "test/integration/ssl_utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "integration.h"
#include "utility.h"

using testing::Return;

namespace Envoy {
namespace Ssl {

void SslIntegrationTest::initialize() {
  config_helper_.addSslConfig();
  HttpIntegrationTest::initialize();

  runtime_.reset(new NiceMock<Runtime::MockLoader>());
  context_manager_.reset(new ContextManagerImpl(*runtime_));

  registerTestServerPorts({"http"});
  client_ssl_ctx_plain_ = createClientSslTransportSocketFactory(false, false, *context_manager_);
  client_ssl_ctx_alpn_ = createClientSslTransportSocketFactory(true, false, *context_manager_);
  client_ssl_ctx_san_ = createClientSslTransportSocketFactory(false, true, *context_manager_);
  client_ssl_ctx_alpn_san_ = createClientSslTransportSocketFactory(true, true, *context_manager_);
}

void SslIntegrationTest::TearDown() {
  test_server_.reset();
  fake_upstreams_.clear();
  client_ssl_ctx_plain_.reset();
  client_ssl_ctx_alpn_.reset();
  client_ssl_ctx_san_.reset();
  client_ssl_ctx_alpn_san_.reset();
  context_manager_.reset();
  runtime_.reset();
}

Network::ClientConnectionPtr SslIntegrationTest::makeSslClientConnection(bool alpn, bool san) {
  Network::Address::InstanceConstSharedPtr address = getSslAddress(version_, lookupPort("http"));
  if (alpn) {
    return dispatcher_->createClientConnection(
        address, Network::Address::InstanceConstSharedPtr(),
        san ? client_ssl_ctx_alpn_san_->createTransportSocket()
            : client_ssl_ctx_alpn_->createTransportSocket(),
        nullptr);
  } else {
    return dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                               san ? client_ssl_ctx_san_->createTransportSocket()
                                                   : client_ssl_ctx_plain_->createTransportSocket(),
                                               nullptr);
  }
}

void SslIntegrationTest::checkStats() {
  if (version_ == Network::Address::IpVersion::v4) {
    Stats::CounterSharedPtr counter = test_server_->counter("listener.127.0.0.1_0.ssl.handshake");
    EXPECT_EQ(1U, counter->value());
    counter->reset();
  } else {
    // ':' is a reserved char in statsd.
    Stats::CounterSharedPtr counter = test_server_->counter("listener.[__1]_0.ssl.handshake");
    EXPECT_EQ(1U, counter->value());
    counter->reset();
  }
}

INSTANTIATE_TEST_CASE_P(IpVersions, SslIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(SslIntegrationTest, RouterRequestAndResponseWithGiantBodyBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(false, false);
  };
  testRouterRequestAndResponseWithBody(16 * 1024 * 1024, 16 * 1024 * 1024, false, &creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(false, false);
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBufferHttp2) {
  setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
  config_helper_.setClientCodec(
      envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::AUTO);
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(true, false);
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBufferVerifySAN) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(false, true);
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBufferHttp2VerifySAN) {
  setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(true, true);
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterHeaderOnlyRequestAndResponse) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(false, false);
  };
  testRouterHeaderOnlyRequestAndResponse(true, &creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(false, false);
  };
  testRouterUpstreamDisconnectBeforeResponseComplete(&creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(false, false);
  };
  testRouterDownstreamDisconnectBeforeRequestComplete(&creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(false, false);
  };
  testRouterDownstreamDisconnectBeforeResponseComplete(&creator);
  checkStats();
}

// This test must be here vs integration_admin_test so that it tests a server with loaded certs.
TEST_P(SslIntegrationTest, AdminCertEndpoint) {
  initialize();
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/certs", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_P(SslIntegrationTest, AltAlpn) {
  // Write the runtime file to turn alt_alpn on.
  TestEnvironment::writeStringToFileForTest("runtime/ssl.alt_alpn", "100");
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
    // Configure the runtime directory.
    bootstrap.mutable_runtime()->set_symlink_root(TestEnvironment::temporaryPath("runtime"));
  });
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(true, false);
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
}

class SslCaptureIntegrationTest : public SslIntegrationTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* filter_chain =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      // Configure inner SSL transport socket based on existing config.
      envoy::api::v2::core::TransportSocket ssl_transport_socket;
      ssl_transport_socket.set_name("ssl");
      MessageUtil::jsonConvert(filter_chain->tls_context(), *ssl_transport_socket.mutable_config());
      // Configure outer capture transport socket.
      auto* transport_socket = filter_chain->mutable_transport_socket();
      transport_socket->set_name("envoy.transport_sockets.capture");
      envoy::config::transport_socket::capture::v2::Capture capture_config;
      capture_config.set_path_prefix(path_prefix_);
      capture_config.set_text_format(text_format_);
      capture_config.mutable_transport_socket()->MergeFrom(ssl_transport_socket);
      MessageUtil::jsonConvert(capture_config, *transport_socket->mutable_config());
      // Nuke TLS context from legacy location.
      filter_chain->clear_tls_context();
      // Rest of TLS initialization.
    });
    SslIntegrationTest::initialize();
  }

  std::string path_prefix_ = TestEnvironment::temporaryPath("ssl_trace");
  bool text_format_{};
};

INSTANTIATE_TEST_CASE_P(IpVersions, SslCaptureIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

// Validate two back-to-back requests with binary proto output.
TEST_P(SslCaptureIntegrationTest, TwoRequestsWithBinaryProto) {
  initialize();
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(false, false);
  };

  // First request.
  codec_client_ = makeHttpConnection(creator());
  Http::TestHeaderMapImpl post_request_headers{
      {":method", "POST"},    {":path", "/test/long/url"}, {":scheme", "http"},
      {":authority", "host"}, {"x-lyft-user-id", "123"},   {"x-forwarded-for", "10.0.0.1"}};
  sendRequestAndWaitForResponse(post_request_headers, 128, default_response_headers_, 256);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(128, upstream_request_->bodyLength());
  ASSERT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  EXPECT_EQ(256, response_->body().size());
  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);
  envoy::extensions::transport_socket::capture::v2alpha::Trace trace;
  MessageUtil::loadFromFile(path_prefix_ + "_0.pb", trace);
  EXPECT_TRUE(absl::StartsWith(trace.events(0).read().data(), "POST /test/long/url HTTP/1.1"));
  EXPECT_TRUE(absl::StartsWith(trace.events(1).write().data(), "HTTP/1.1 200 OK"));

  // Verify a second request hits a different file.
  codec_client_ = makeHttpConnection(creator());
  Http::TestHeaderMapImpl get_request_headers{
      {":method", "GET"},     {":path", "/test/long/url"}, {":scheme", "http"},
      {":authority", "host"}, {"x-lyft-user-id", "123"},   {"x-forwarded-for", "10.0.0.1"}};
  sendRequestAndWaitForResponse(get_request_headers, 128, default_response_headers_, 256);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(128, upstream_request_->bodyLength());
  ASSERT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  EXPECT_EQ(256, response_->body().size());
  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 2);
  MessageUtil::loadFromFile(path_prefix_ + "_1.pb", trace);
  EXPECT_TRUE(absl::StartsWith(trace.events(0).read().data(), "GET /test/long/url HTTP/1.1"));
  EXPECT_TRUE(absl::StartsWith(trace.events(1).write().data(), "HTTP/1.1 200 OK"));
}

// Validate a single request with text proto output.
TEST_P(SslCaptureIntegrationTest, RequestWithTextProto) {
  text_format_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(false, false);
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);
  envoy::extensions::transport_socket::capture::v2alpha::Trace trace;
  MessageUtil::loadFromFile(path_prefix_ + "_0.pb_text", trace);
  EXPECT_TRUE(absl::StartsWith(trace.events(0).read().data(), "POST /test/long/url HTTP/1.1"));
  EXPECT_TRUE(absl::StartsWith(trace.events(1).write().data(), "HTTP/1.1 200 OK"));
}

} // namespace Ssl
} // namespace Envoy
