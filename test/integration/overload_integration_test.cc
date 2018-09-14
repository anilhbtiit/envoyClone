#include "test/integration/http_protocol_integration.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"

namespace Envoy {

class OverloadIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  OverloadIntegrationTest()
      : injected_resource_filename_(TestEnvironment::temporaryPath("injected_resource")),
        file_updater_(injected_resource_filename_) {}

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      const std::string overload_config = absl::Substitute(R"EOF(
        refresh_interval {
          seconds: 0
          nanos: 1000000
        }
        resource_monitors {
          name: "envoy.resource_monitors.injected_resource"
          config {
            fields {
              key: "filename"
              value {
                string_value: "$0"
              }
            }
          }
        }
        actions {
          name: "envoy.overload_actions.stop_accepting_requests"
          triggers {
            name: "envoy.resource_monitors.injected_resource"
            threshold {
              value: 0.9
            }
          }
        }
      )EOF",
                                                           injected_resource_filename_);
      ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(overload_config,
                                                        bootstrap.mutable_overload_manager()));
    });
    updateResource(0);
    HttpIntegrationTest::initialize();
  }

  void updateResource(double pressure) { file_updater_.update(absl::StrCat(pressure)); }

  const std::string injected_resource_filename_;
  AtomicFileUpdater file_updater_;
};

INSTANTIATE_TEST_CASE_P(Protocols, OverloadIntegrationTest,
                        testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                        HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(OverloadIntegrationTest, CloseStreamsWhenOverloaded) {
  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  // Put envoy in overloaded state and check that it drops new requests.
  updateResource(0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_requests.active", 1);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, 10);
  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
  EXPECT_EQ("envoy overloaded", response->body());
  codec_client_->close();

  // Deactivate overload state and check that new requests are accepted.
  updateResource(0.8);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_requests.active", 0);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(0U, response->body().size());
}

} // namespace Envoy
