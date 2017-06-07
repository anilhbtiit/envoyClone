#include "xfcc_integration_test.h"

#include <stdio.h>
#include <regex>

#include "common/event/dispatcher_impl.h"
#include "common/http/header_map_impl.h"
#include "common/network/utility.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_manager_impl.h"
#include "ssl_integration_test.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "integration.h"
#include "utility.h"

namespace Envoy {

namespace Xfcc {

void XfccIntegrationTest::SetUp() {
  runtime_.reset(new NiceMock<Runtime::MockLoader>());
  context_manager_.reset(new Ssl::ContextManagerImpl(*runtime_));
  upstream_ssl_ctx_ = createUpstreamSslContext();
  fake_upstreams_.emplace_back(new FakeUpstream(upstream_ssl_ctx_.get(), 0, FakeHttpConnection::Type::HTTP1));
  registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
  std::string config = TestEnvironment::temporaryFileSubstitute(
      "test/config/integration/server_xfcc.json", port_map_);
  test_server_ = Ssl::MockRuntimeIntegrationTestServer::create(config, Network::Address::IpVersion::v4);
  registerTestServerPorts({"http"});
}

void XfccIntegrationTest::TearDown() {
  test_server_.reset();
  fake_upstreams_.clear();
  upstream_ssl_ctx_.reset();
  context_manager_.reset();
  runtime_.reset();
}

Ssl::ClientContextPtr XfccIntegrationTest::createClientSslContext() {
  std::string json_san = R"EOF(
{
  "ca_cert_file": "{{ test_rundir }}/test/config/integration/certs/cacert.pem",
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/clientcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/clientkey.pem",
  "verify_subject_alt_name": [ "istio:account_a.namespace_foo.cluster.local" ]
}
)EOF";
 
  Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(json_san);
  Ssl::ContextConfigImpl cfg(*loader);
  static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
  return context_manager_->createSslClientContext(*client_stats_store, cfg);
}

Ssl::ServerContextPtr XfccIntegrationTest::createUpstreamSslContext() {
  static auto* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
  std::string json = R"EOF(
{
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/upstreamcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/upstreamkey.pem"
}
)EOF";

  Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(json);
  Ssl::ContextConfigImpl cfg(*loader);
  return context_manager_->createSslServerContext(*upstream_stats_store, cfg);
}

void XfccIntegrationTest::testRequestAndResponseWithXfccHeader(
    Network::ClientConnectionPtr&& conn, std::string expected_xfcc) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr upstream_request;
  executeActions(
      {[&]() -> void {
         codec_client = makeHttpConnection(std::move(conn), Http::CodecClient::Type::HTTP1);
       },
       [&]() -> void {
         codec_client->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                     {":path", "/test/long/url"},
                                                                     {":scheme", "http"},
                                                                     {":authority", "host"},
                                                                     {"x-forwarded-client-cert",
                                                                      xfcc_header_.c_str()}},
                                             *response);
       },
       [&]() -> void { fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_); },
       [&]() -> void { upstream_request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { upstream_request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         if (expected_xfcc.empty()) {
           EXPECT_EQ(nullptr, upstream_request->headers().ForwardedClientCert());
         } else {
           EXPECT_STREQ(expected_xfcc.c_str(), upstream_request->headers().ForwardedClientCert()->value().c_str());
        }
         upstream_request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
       },
       [&]() -> void {
         response->waitForEndStream();
         EXPECT_TRUE(upstream_request->complete());
       },

       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});
  EXPECT_TRUE(response->complete());
}

void XfccIntegrationTest::modifyXfccConfigs(std::string fcc, std::string sccd) {
  test_server_.reset();
  std::string config = TestEnvironment::temporaryFileSubstitute(
      "test/config/integration/server_xfcc.json", port_map_);
  printf("Oliver:\n%s\nabc\n", config.c_str());
  const std::regex fcc_regex("forward_only");
  config = std::regex_replace(config, fcc_regex, fcc);
  const std::regex sccd_regex("SAN");
  config = std::regex_replace(config, sccd_regex, sccd);
  printf("Oliver:\n%s\nabc\n", config.c_str());
  test_server_ = Ssl::MockRuntimeIntegrationTestServer::create(config, Network::Address::IpVersion::v4);
  registerTestServerPorts({"http"});
}

TEST_F(XfccIntegrationTest, ForwardOnly) {
  testRequestAndResponseWithXfccHeader(
      dispatcher_->createSslClientConnection(
          *createClientSslContext(),
          Network::Utility::resolveUrl("tcp://127.0.0.1:" + std::to_string(lookupPort("http")))),
      xfcc_header_);
}

TEST_F(XfccIntegrationTest, Sanitize) {
  modifyXfccConfigs("sanitize", "SAN");
  testRequestAndResponseWithXfccHeader(
      dispatcher_->createSslClientConnection(
          *createClientSslContext(),
          Network::Utility::resolveUrl("tcp://127.0.0.1:" + std::to_string(lookupPort("http")))), "");
}
} // Xfcc
} // Envoy
