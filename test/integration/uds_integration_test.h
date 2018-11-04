#pragma once

#include <tuple>

#include "common/common/fmt.h"
#include "common/http/codec_client.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {

class UdsUpstreamIntegrationTest
    : public HttpIntegrationTest,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>> {
public:
  UdsUpstreamIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, std::get<0>(GetParam()), realTime()),
        abstract_namespace_(std::get<1>(GetParam())) {}

  void createUpstreams() override {
    fake_upstreams_.emplace_back(new FakeUpstream(
        TestEnvironment::unixDomainSocketPath("udstest.1.sock", abstract_namespace_),
        FakeHttpConnection::Type::HTTP1, timeSystem()));

    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
          auto* static_resources = bootstrap.mutable_static_resources();
          for (int i = 0; i < static_resources->clusters_size(); ++i) {
            auto* cluster = static_resources->mutable_clusters(i);
            auto* load_assignment = cluster->mutable_load_assignment();
            for (int j = 0; j < load_assignment->endpoints_size(); ++j) {
              auto* locality_lb_endpoint = load_assignment->mutable_endpoints(j);
              for (int k = 0; k < locality_lb_endpoint->lb_endpoints_size(); ++k) {
                auto* lb_endpoint = locality_lb_endpoint->mutable_lb_endpoints(k);
                // TODO(dio): Make lb_endpoint's endpoint and address non-nullable.
                auto* address = lb_endpoint->mutable_endpoint()->mutable_address();
                address->clear_socket_address();
                address->mutable_pipe()->set_path(
                    TestEnvironment::unixDomainSocketPath("udstest.1.sock", abstract_namespace_));
              }
            }
          }
        });
  }

protected:
  const bool abstract_namespace_;
};

class UdsListenerIntegrationTest
    : public HttpIntegrationTest,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>> {
public:
  UdsListenerIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, std::get<0>(GetParam()), realTime()),
        abstract_namespace_(std::get<1>(GetParam())) {}

  void initialize() override;

  std::string getAdminSocketName() {
    return TestEnvironment::unixDomainSocketPath("admin.sock", abstract_namespace_);
  }

  std::string getListenerSocketName() {
    return TestEnvironment::unixDomainSocketPath("listener_0.sock", abstract_namespace_);
  }

protected:
  HttpIntegrationTest::ConnectionCreationFunction createConnectionFn();

  const bool abstract_namespace_;
};

} // namespace Envoy
