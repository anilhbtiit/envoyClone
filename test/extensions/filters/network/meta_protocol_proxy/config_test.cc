#include "source/extensions/filters/network/meta_protocol_proxy/config.h"

#include "test/extensions/filters/network/meta_protocol_proxy/fake_codec.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {
namespace {

// Keep empty until merge the latest API from main.
TEST(FactoryTest, FactoryTest) {
  const std::string yaml_config = R"EOF(
    stat_prefix: config_test
    filters:
    - name: envoy.filters.meta.router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.router.v3.Router
    codec_config:
      name: fake
      typed_config:
        "@type": type.googleapis.com/xds.type.v3.TypedStruct
        type_url: envoy.meta_protocol_proxy.codec.fake.type
        value: {}
    route_config:
      name: test-routes
      routes:
        matcher_tree:
          input:
            name: request-service
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.matcher.v3.ServiceMatchInput
          exact_match_map:
            map:
              service_name_0:
                matcher:
                  matcher_list:
                    matchers:
                    - predicate:
                        single_predicate:
                          input:
                            name: request-properties
                            typed_config:
                              "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.matcher.v3.PropertyMatchInput
                              property_name: version
                          value_match:
                            exact: v1
                      on_match:
                        action:
                          name: route
                          typed_config:
                            "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.action.v3.RouteAction
                            cluster: cluster_0
    )EOF";

  FakeStreamCodecFactoryConfig codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  Factory factory;
  ProxyConfig proto_config;
  TestUtility::loadFromYaml(yaml_config, proto_config);

  EXPECT_NE(nullptr, factory.createFilterFactoryFromProto(proto_config, factory_context));
}

} // namespace
} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
