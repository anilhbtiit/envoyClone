#include "test/integration/http_protocol_integration.h"

namespace Envoy {

class RedirectIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    envoy::api::v2::route::RouteAction::RetryPolicy retry_policy;

    // Add route with custom retry policy
    config_helper_.addRoute("reject.redirect", "/", "cluster_0", false,
                            envoy::api::v2::route::RouteAction::NOT_FOUND,
                            envoy::api::v2::route::VirtualHost::NONE, retry_policy, false, "",
                            envoy::api::v2::route::RouteAction::REJECT);

    // Add route with custom retry policy
    config_helper_.addRoute("pass.through.redirect", "/", "cluster_0", false,
                            envoy::api::v2::route::RouteAction::NOT_FOUND,
                            envoy::api::v2::route::VirtualHost::NONE, retry_policy, false, "",
                            envoy::api::v2::route::RouteAction::PASS_THROUGH);

    // Add route with custom retry policy
    config_helper_.addRoute("handle.redirect", "/", "cluster_0", false,
                            envoy::api::v2::route::RouteAction::NOT_FOUND,
                            envoy::api::v2::route::VirtualHost::NONE, retry_policy, false, "",
                            envoy::api::v2::route::RouteAction::HANDLE);

    HttpProtocolIntegrationTest::initialize();
  }

protected:
  Http::TestHeaderMapImpl default_redirect_response_{{":status", "302"},
                                                     {"x-envoy-internal-redirect", "yes"},
                                                     {"location", "http://authority2/new/url"}};
};

// By default if internal redirects are not configured, redirects are translated
// into error responses lest Envoy leak upstream information.
TEST_P(RedirectIntegrationTest, RedirectNotConfigured) {
  // Use base class initialize.
  HttpProtocolIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send a basic request
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // Send a redirect response from upstream.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_redirect_response_, true);

  // The redirect will be transformed into a server error because redirects are
  // not configured on.
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("500", response->headers().Status()->value().c_str());
}

// Now test a route with redirects explicitly disabled.
TEST_P(RedirectIntegrationTest, RedirectExplicitlyDisabled) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.insertHost().value("reject.redirect", 15);
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // Send a redirect response from upstream.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_redirect_response_, true);

  // The redirect will be transformed into a server error because redirects are
  // not configured on.
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("500", response->headers().Status()->value().c_str());
  ASSERT_TRUE(response->headers().EnvoyInternalRedirect() == nullptr);
}

// Now test a route with redirects in pass-through mode..
TEST_P(RedirectIntegrationTest, RedirectPassedThrough) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.insertHost().value("pass.through.redirect", 21);
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // Send a redirect response from upstream.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_redirect_response_, true);

  // The redirect will be transformed into a server error because redirects are
  // not configured on.
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("302", response->headers().Status()->value().c_str());
  ASSERT_TRUE(response->headers().EnvoyInternalRedirect() != nullptr);
}

TEST_P(RedirectIntegrationTest, BasicRedirect) {
  // Validate that header sanitization is only called once.
  config_helper_.addConfigModifier(
      [](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm) {
        hcm.set_via("via_value");
      });
  // FIXME fixed #5037
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    return;
  }
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.insertHost().value("handle.redirect", 15);
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_redirect_response_, true);

  waitForNextUpstreamRequest();
  ASSERT(upstream_request_->headers().EnvoyOriginalUrl() != nullptr);
  EXPECT_STREQ("http://handle.redirect/test/long/url",
               upstream_request_->headers().EnvoyOriginalUrl()->value().c_str());
  EXPECT_STREQ("/new/url", upstream_request_->headers().Path()->value().c_str());
  EXPECT_STREQ("authority2", upstream_request_->headers().Host()->value().c_str());
  EXPECT_STREQ("via_value", upstream_request_->headers().Via()->value().c_str());

  upstream_request_->encodeHeaders(default_response_headers_, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_P(RedirectIntegrationTest, InvalidRedirect) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.insertHost().value("handle.redirect", 15);
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  default_redirect_response_.insertLocation().value("invalid_url", 11);
  upstream_request_->encodeHeaders(default_redirect_response_, true);

  // The redirect will be transformed into a server error because the url was
  // invalid.
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("500", response->headers().Status()->value().c_str());
  ASSERT_TRUE(response->headers().EnvoyInternalRedirect() == nullptr);
}

INSTANTIATE_TEST_CASE_P(Protocols, RedirectIntegrationTest,
                        testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                        HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace Envoy
