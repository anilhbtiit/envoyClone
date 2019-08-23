#include "envoy/api/v2/srds.pb.h"

#include "common/config/resources.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

class ScopedRdsIntegrationTest : public HttpIntegrationTest,
                                 public Grpc::GrpcClientIntegrationParamTest {
protected:
  struct FakeUpstreamInfo {
    FakeHttpConnectionPtr connection_;
    FakeUpstream* upstream_{};
    absl::flat_hash_map<std::string, FakeStreamPtr> stream_by_resource_name_;
  };

  ScopedRdsIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion(), realTime()) {}

  ~ScopedRdsIntegrationTest() override {
    resetConnections();
    cleanupUpstreamAndDownstream();
  }

  void initialize() override {
    // Setup two upstream hosts, one for each cluster.
    setUpstreamCount(2);

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      // Add the static cluster to serve SRDS.
      auto* cluster_1 = bootstrap.mutable_static_resources()->add_clusters();
      cluster_1->MergeFrom(bootstrap.static_resources().clusters()[0]);
      cluster_1->set_name("cluster_1");

      // Add the static cluster to serve SRDS.
      auto* scoped_rds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      scoped_rds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      scoped_rds_cluster->set_name("srds_cluster");
      scoped_rds_cluster->mutable_http2_protocol_options();

      // Add the static cluster to serve RDS.
      auto* rds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      rds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      rds_cluster->set_name("rds_cluster");
      rds_cluster->mutable_http2_protocol_options();
    });

    config_helper_.addConfigModifier(
        [this](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
                   http_connection_manager) {
          const std::string& scope_key_builder_config_yaml = R"EOF(
fragments:
  - header_value_extractor:
      name: Addr
      element_separator: ;
      element:
        key: x-foo-key
        separator: =
)EOF";
          envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes::ScopeKeyBuilder
              scope_key_builder;
          TestUtility::loadFromYaml(scope_key_builder_config_yaml, scope_key_builder);
          auto* scoped_routes = http_connection_manager.mutable_scoped_routes();
          scoped_routes->set_name(srds_config_name_);
          *scoped_routes->mutable_scope_key_builder() = scope_key_builder;

          envoy::api::v2::core::ApiConfigSource* rds_api_config_source =
              scoped_routes->mutable_rds_config_source()->mutable_api_config_source();
          rds_api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
          envoy::api::v2::core::GrpcService* grpc_service =
              rds_api_config_source->add_grpc_services();
          setGrpcService(*grpc_service, "rds_cluster", getRdsFakeUpstream().localAddress());

          envoy::api::v2::core::ApiConfigSource* srds_api_config_source =
              scoped_routes->mutable_scoped_rds()
                  ->mutable_scoped_rds_config_source()
                  ->mutable_api_config_source();
          srds_api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
          grpc_service = srds_api_config_source->add_grpc_services();
          setGrpcService(*grpc_service, "srds_cluster", getScopedRdsFakeUpstream().localAddress());
        });

    HttpIntegrationTest::initialize();
  }

  // Helper that verifies if given headers are in the response header map.
  void verifyResponse(IntegrationStreamDecoderPtr response, const std::string& response_code,
                      const Http::TestHeaderMapImpl& expected_headers,
                      const std::string& expected_body) {
    EXPECT_TRUE(response->complete());
    EXPECT_EQ(response_code, response->headers().Status()->value().getStringView());
    expected_headers.iterate(
        [](const Http::HeaderEntry& header, void* context) -> Http::HeaderMap::Iterate {
          auto response_headers = static_cast<Http::HeaderMap*>(context);
          const Http::HeaderEntry* entry = response_headers->get(
              Http::LowerCaseString{std::string(header.key().getStringView())});
          EXPECT_NE(entry, nullptr);
          EXPECT_EQ(header.value().getStringView(), entry->value().getStringView());
          return Http::HeaderMap::Iterate::Continue;
        },
        const_cast<void*>(static_cast<const void*>(&response->headers())));
    EXPECT_EQ(response->body(), expected_body);
  }

  // Helper that sends a request to Envoy, and verifies if Envoy response headers and body size is
  // the same as the expected headers map.
  void sendRequestAndVerifyResponse(const Http::TestHeaderMapImpl& request_headers,
                                    const int request_size,
                                    const Http::TestHeaderMapImpl& response_headers,
                                    const int response_size, const int backend_idx) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = sendRequestAndWaitForResponse(request_headers, request_size, response_headers,
                                                  response_size, backend_idx);
    verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(request_size, upstream_request_->bodyLength());
    cleanupUpstreamAndDownstream();
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Create the SRDS upstream.
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_,
                                                  timeSystem(), enable_half_close_));
    // Create the RDS upstream.
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_,
                                                  timeSystem(), enable_half_close_));
  }

  void resetFakeUpstreamInfo(FakeUpstreamInfo* upstream_info) {
    ASSERT(upstream_info->upstream_ != nullptr);

    upstream_info->upstream_->set_allow_unexpected_disconnects(true);
    AssertionResult result = upstream_info->connection_->close();
    RELEASE_ASSERT(result, result.message());
    result = upstream_info->connection_->waitForDisconnect();
    RELEASE_ASSERT(result, result.message());
    upstream_info->connection_.reset();
  }

  void resetConnections() {
    if (rds_upstream_info_.upstream_ != nullptr) {
      resetFakeUpstreamInfo(&rds_upstream_info_);
    }
    resetFakeUpstreamInfo(&scoped_rds_upstream_info_);
  }

  FakeUpstream& getRdsFakeUpstream() const { return *fake_upstreams_[3]; }

  FakeUpstream& getScopedRdsFakeUpstream() const { return *fake_upstreams_[2]; }

  void createStream(FakeUpstreamInfo* upstream_info, FakeUpstream& upstream,
                    const std::string& resource_name) {
    if (upstream_info->upstream_ == nullptr) {
      // bind upstream if not yet.
      upstream_info->upstream_ = &upstream;
      AssertionResult result =
          upstream_info->upstream_->waitForHttpConnection(*dispatcher_, upstream_info->connection_);
      RELEASE_ASSERT(result, result.message());
    }
    if (!upstream_info->stream_by_resource_name_.try_emplace(resource_name, nullptr).second) {
      RELEASE_ASSERT(false,
                     fmt::format("stream with resource name '{}' already exists!", resource_name));
    }
    auto result = upstream_info->connection_->waitForNewStream(
        *dispatcher_, upstream_info->stream_by_resource_name_[resource_name]);
    RELEASE_ASSERT(result, result.message());
    upstream_info->stream_by_resource_name_[resource_name]->startGrpcStream();
  }

  void createRdsStream(const std::string& resource_name) {
    createStream(&rds_upstream_info_, getRdsFakeUpstream(), resource_name);
  }

  void createScopedRdsStream() {
    createStream(&scoped_rds_upstream_info_, getScopedRdsFakeUpstream(), srds_config_name_);
  }

  void sendRdsResponse(const std::string& route_config, const std::string& version) {
    envoy::api::v2::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url(Config::TypeUrl::get().RouteConfiguration);
    auto route_configuration =
        TestUtility::parseYaml<envoy::api::v2::RouteConfiguration>(route_config);
    response.add_resources()->PackFrom(route_configuration);
    ASSERT(rds_upstream_info_.stream_by_resource_name_[route_configuration.name()] != nullptr);
    rds_upstream_info_.stream_by_resource_name_[route_configuration.name()]->sendGrpcMessage(
        response);
  }

  void sendScopedRdsResponse(const std::vector<std::string>& resource_protos,
                             const std::string& version) {
    ASSERT(scoped_rds_upstream_info_.stream_by_resource_name_[srds_config_name_] != nullptr);

    envoy::api::v2::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url(Config::TypeUrl::get().ScopedRouteConfiguration);

    for (const auto& resource_proto : resource_protos) {
      envoy::api::v2::ScopedRouteConfiguration scoped_route_proto;
      TestUtility::loadFromYaml(resource_proto, scoped_route_proto);
      response.add_resources()->PackFrom(scoped_route_proto);
    }
    scoped_rds_upstream_info_.stream_by_resource_name_[srds_config_name_]->sendGrpcMessage(
        response);
  }

  const std::string srds_config_name_{"foo-scoped-routes"};
  FakeUpstreamInfo scoped_rds_upstream_info_;
  FakeUpstreamInfo rds_upstream_info_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsAndGrpcTypes, ScopedRdsIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// Test that a SRDS DiscoveryResponse is successfully processed.
TEST_P(ScopedRdsIntegrationTest, BasicSuccess) {
  const std::string scope_tmpl = R"EOF(
name: {}
route_configuration_name: {}
key:
  fragments:
    - string_key: {}
)EOF";
  const std::string scope_route1 = fmt::format(scope_tmpl, "foo_scope1", "foo_route1", "foo-route");
  const std::string scope_route2 = fmt::format(scope_tmpl, "foo_scope2", "foo_route1", "bar-route");

  const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
)EOF";

  on_server_init_function_ = [&]() {
    createScopedRdsStream();
    sendScopedRdsResponse({scope_route1, scope_route2}, "1");
    createRdsStream("foo_route1");
    // CreateRdsStream waits for connection which is fired by RDS subscription.
    sendRdsResponse(fmt::format(route_config_tmpl, "foo_route1", "cluster_0"), "1");
  };
  initialize();
  registerTestServerPorts({"http"});

  // No scope key matches "xyz-route".
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestHeaderMapImpl{{":method", "GET"},
                              {":path", "/meh"},
                              {":authority", "host"},
                              {":scheme", "http"},
                              {"Addr", "x-foo-key=xyz-route"}});
  response->waitForEndStream();
  verifyResponse(std::move(response), "404", Http::TestHeaderMapImpl{}, "route scope not found");
  cleanupUpstreamAndDownstream();

  // Test "foo-route" and 'bar-route' both gets routed to cluster_0.
  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_success", 1);
  for (const std::string& scope_key : std::vector<std::string>{"foo-route", "bar-route"}) {
    sendRequestAndVerifyResponse(
        Http::TestHeaderMapImpl{{":method", "GET"},
                                {":path", "/meh"},
                                {":authority", "host"},
                                {":scheme", "http"},
                                {"Addr", fmt::format("x-foo-key={}", scope_key)}},
        456, Http::TestHeaderMapImpl{{":status", "200"}, {"service", scope_key}}, 123,
        /*cluster_0*/ 0);
  }
  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_attempt", 2);
  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_success", 1);
  // The version gauge should be set to xxHash64("1").
  test_server_->waitForGaugeEq("http.config_test.scoped_rds.foo-scoped-routes.version",
                               13237225503670494420UL);

  // Add a new scope scope_route3 with a brand new RouteConfiguration foo_route2.
  const std::string scope_route3 = fmt::format(scope_tmpl, "foo_scope3", "foo_route2", "baz-route");

  sendScopedRdsResponse({scope_route3, scope_route1, scope_route2}, "2");
  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_attempt", 2);
  sendRdsResponse(fmt::format(route_config_tmpl, "foo_route1", "cluster_1"), "3");
  createRdsStream("foo_route2");
  test_server_->waitForCounterGe("http.config_test.rds.foo_route2.update_attempt", 1);
  sendRdsResponse(fmt::format(route_config_tmpl, "foo_route2", "cluster_0"), "1");
  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_success", 2);
  test_server_->waitForCounterGe("http.config_test.rds.foo_route2.update_success", 1);
  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_success", 2);
  // The version gauge should be set to xxHash64("2").
  test_server_->waitForGaugeEq("http.config_test.scoped_rds.foo-scoped-routes.version",
                               6927017134761466251UL);
  // After RDS update, requests within scope 'foo_scope1' or 'foo_scope2' get routed to 'cluster_1'.
  for (const std::string& scope_key : std::vector<std::string>{"foo-route", "bar-route"}) {
    sendRequestAndVerifyResponse(
        Http::TestHeaderMapImpl{{":method", "GET"},
                                {":path", "/meh"},
                                {":authority", "host"},
                                {":scheme", "http"},
                                {"Addr", fmt::format("x-foo-key={}", scope_key)}},
        456, Http::TestHeaderMapImpl{{":status", "200"}, {"service", scope_key}}, 123,
        /*cluster_1*/ 1);
  }
  // Now requests within scope 'foo_scope3' get routed to 'cluster_0'.
  test_server_->waitForCounterGe("http.config_test.rds.foo_route2.update_success", 1);
  sendRequestAndVerifyResponse(
      Http::TestHeaderMapImpl{{":method", "GET"},
                              {":path", "/meh"},
                              {":authority", "host"},
                              {":scheme", "http"},
                              {"Addr", fmt::format("x-foo-key={}", "baz-route")}},
      456, Http::TestHeaderMapImpl{{":status", "200"}, {"service", "bluh"}}, 123, /*cluster_0*/ 0);

  // Delete foo_scope1 and requests within the scope gets 400s.
  sendScopedRdsResponse({scope_route3, scope_route2}, "3");
  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_success", 3);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  response = codec_client_->makeHeaderOnlyRequest(
      Http::TestHeaderMapImpl{{":method", "GET"},
                              {":path", "/meh"},
                              {":authority", "host"},
                              {":scheme", "http"},
                              {"Addr", "x-foo-key=foo-route"}});
  response->waitForEndStream();
  verifyResponse(std::move(response), "404", Http::TestHeaderMapImpl{}, "route scope not found");
  cleanupUpstreamAndDownstream();
  // Add a new scope foo_scope4.
  const std::string& scope_route4 =
      fmt::format(scope_tmpl, "foo_scope4", "foo_route4", "xyz-route");
  sendScopedRdsResponse({scope_route3, scope_route2, scope_route4}, "4");
  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_success", 4);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  response = codec_client_->makeHeaderOnlyRequest(
      Http::TestHeaderMapImpl{{":method", "GET"},
                              {":path", "/meh"},
                              {":authority", "host"},
                              {":scheme", "http"},
                              {"Addr", "x-foo-key=xyz-route"}});
  response->waitForEndStream();
  // Get 404 because RDS hasn't pushed route configuration "foo_route4" yet.
  // But scope is found and the Router::NullConfigImpl is returned.
  verifyResponse(std::move(response), "404", Http::TestHeaderMapImpl{}, "");
  cleanupUpstreamAndDownstream();

  // RDS updated foo_route4, requests with socpe key "xyz-route" now hit cluster_1.
  test_server_->waitForCounterGe("http.config_test.rds.foo_route4.update_attempt", 1);
  createRdsStream("foo_route4");
  sendRdsResponse(fmt::format(route_config_tmpl, "foo_route4", "cluster_1"), "3");
  test_server_->waitForCounterGe("http.config_test.rds.foo_route4.update_success", 1);
  sendRequestAndVerifyResponse(
      Http::TestHeaderMapImpl{{":method", "GET"},
                              {":path", "/meh"},
                              {":authority", "host"},
                              {":scheme", "http"},
                              {"Addr", "x-foo-key=xyz-route"}},
      456, Http::TestHeaderMapImpl{{":status", "200"}, {"service", "xyz-route"}}, 123,
      /*cluster_1 */ 1);
}

// Test that a bad config update updates the corresponding stats.
TEST_P(ScopedRdsIntegrationTest, ConfigUpdateFailure) {
  // 'name' will fail to validate due to empty string.
  const std::string scope_route1 = R"EOF(
name:
route_configuration_name: foo_route1
key:
  fragments:
    - string_key: foo
)EOF";
  on_server_init_function_ = [this, &scope_route1]() {
    createScopedRdsStream();
    sendScopedRdsResponse({scope_route1}, "1");
  };
  initialize();

  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_rejected",
                                 1);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                   {":path", "/meh"},
                                                                   {":authority", "host"},
                                                                   {":scheme", "http"},
                                                                   {"Addr", "x-foo-key=foo"}});
  response->waitForEndStream();
  verifyResponse(std::move(response), "404", Http::TestHeaderMapImpl{}, "route scope not found");
  cleanupUpstreamAndDownstream();

  // SRDS update fixed the problem.
  const std::string scope_route2 = R"EOF(
name: foo_scope1
route_configuration_name: foo_route1
key:
  fragments:
    - string_key: foo
)EOF";
  sendScopedRdsResponse({scope_route2}, "2");
  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_attempt", 1);
  createRdsStream("foo_route1");
  const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
)EOF";
  sendRdsResponse(fmt::format(route_config_tmpl, "foo_route1", "cluster_0"), "1");
  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_success", 1);
  sendRequestAndVerifyResponse(
      Http::TestHeaderMapImpl{{":method", "GET"},
                              {":path", "/meh"},
                              {":authority", "host"},
                              {":scheme", "http"},
                              {"Addr", "x-foo-key=foo"}},
      456, Http::TestHeaderMapImpl{{":status", "200"}, {"service", "bluh"}}, 123, /*cluster_0*/ 0);
}

} // namespace
} // namespace Envoy
