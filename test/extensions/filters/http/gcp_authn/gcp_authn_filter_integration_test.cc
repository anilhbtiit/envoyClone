#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"

#include "test/integration/http_integration.h"
#include "test/mocks/server/options.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {
namespace {

using testing::TestWithParam;

class GcpAuthnFilterIntegrationTest : public TestWithParam<Network::Address::IpVersion>,
                                      public HttpIntegrationTest {
public:
  GcpAuthnFilterIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

  void createUpstreams() override {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    //  Add two fake upstreams, the second one is for token stream
    for (int i = 0; i < 2; ++i) {
      addFakeUpstream(FakeHttpConnection::Type::HTTP2);
    }
  }

  void initialize() override {
    initializeConfig();
    HttpIntegrationTest::initialize();
  }

  void initializeConfig() {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* gcp_authn_cluster = bootstrap.mutable_static_resources()->add_clusters();
      gcp_authn_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      gcp_authn_cluster->set_name("gcp_authn");
      ConfigHelper::setHttp2(*gcp_authn_cluster);

      TestUtility::loadFromYaml(default_config_, proto_config_);
      envoy::config::listener::v3::Filter gcp_authn_filter;
      gcp_authn_filter.set_name("envoy.filters.http.gcp_authn");
      gcp_authn_filter.mutable_typed_config()->PackFrom(proto_config_);

      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrDie(gcp_authn_filter));
    });
  }

  void initiateClientConnection() {
    // Create a client aimed at Envoy’s default HTTP port.
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
    Http::TestRequestHeaderMapImpl headers{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
    response_ = codec_client_->makeHeaderOnlyRequest(headers);
  }

  void waitForTokenResponse() {
    AssertionResult result =
        fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_gcp_authn_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_gcp_authn_connection_->waitForNewStream(*dispatcher_, token_request_);
    RELEASE_ASSERT(result, result.message());
    result = token_request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());

    // Http::TestResponseHeaderMapImpl response_headers{{":status", status}};
    // jwks_request_->encodeHeaders(response_headers, false);
    // Buffer::OwnedImpl response_data1(jwks_body);
    // jwks_request_->encodeData(response_data1, true);
  }

  void sendRequestAndValidateResponse(const std::vector<uint64_t>& upstream_indices) {
    // Create a client aimed at Envoy’s default HTTP port.
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

    // Create some request headers.
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};

    // Send the request headers from the client, wait until they are received
    // upstream. When they are received, send the default response headers from
    // upstream and wait until they are received at by client.
    IntegrationStreamDecoderPtr response = sendRequestAndWaitForResponse(
        request_headers, 0, default_response_headers_, 0, upstream_indices);

    // Verify the proxied request was received upstream, as expected.
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(0U, upstream_request_->bodyLength());
    // Verify the proxied response was received downstream, as expected.
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ(0U, response->body().size());

    // Perform the clean-up.
    cleanupUpstreamAndDownstream();
  }

private:
  IntegrationStreamDecoderPtr response_;
  FakeHttpConnectionPtr fake_gcp_authn_connection_{};
  FakeStreamPtr token_request_{};
  // TODO(tyxia) How do i know which uri address should I send
  const std::string default_config_ = R"EOF(
    http_uri:
      uri: "gcp_authn:9000"
      cluster: gcp_aut
      timeout:
        seconds: 5
  )EOF";
  envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig proto_config_{};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GcpAuthnFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(GcpAuthnFilterIntegrationTest, Basicflow) {
  initialize();
  // // TODO(tyxia) index0 passed but index 1 failed
  sendRequestAndValidateResponse({0});
  // initiateClientConnection();
  // waitForTokenResponse();
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
