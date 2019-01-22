#include "envoy/registry/registry.h"

#include "extensions/tracers/opencensus/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenCensus {

TEST(OpenCensusTracerConfigTest, OpenCensusHttpTracer) {
  NiceMock<Server::MockInstance> server;
  const std::string yaml_string = R"EOF(
  http:
    name: envoy.tracers.opencensus
  )EOF";

  envoy::config::trace::v2::Tracing configuration;
  MessageUtil::loadFromYaml(yaml_string, configuration);

  OpenCensusTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(configuration.http(), factory);
  Tracing::HttpTracerPtr tracer = factory.createHttpTracer(*message, server);
  EXPECT_NE(nullptr, tracer);
}

TEST(OpenCensusTracerConfigTest, OpenCensusHttpTracerWithTypedConfig) {
  NiceMock<Server::MockInstance> server;
  const std::string yaml_string = R"EOF(
  http:
    name: envoy.tracers.opencensus
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v2.OpenCensusConfig
      trace_config:
        constant_sampler:
          decision: true
      stdout_exporter_enabled: true
      propagate_trace_context: true
  )EOF";

  envoy::config::trace::v2::Tracing configuration;
  MessageUtil::loadFromYaml(yaml_string, configuration);

  OpenCensusTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(configuration.http(), factory);
  Tracing::HttpTracerPtr tracer = factory.createHttpTracer(*message, server);
  EXPECT_NE(nullptr, tracer);
}

TEST(OpenCensusTracerConfigTest, DoubleRegistrationTest) {
  EXPECT_THROW_WITH_MESSAGE(
      (Registry::RegisterFactory<OpenCensusTracerFactory, Server::Configuration::TracerFactory>()),
      EnvoyException, "Double registration for name: 'envoy.tracers.opencensus'");
}

} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
