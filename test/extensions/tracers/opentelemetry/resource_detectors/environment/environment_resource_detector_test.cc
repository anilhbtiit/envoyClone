#include <string>

#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/environment_resource_detector.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/resource_detectors/environment/environment_resource_detector.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

const std::string kOtelResourceAttributesEnv = "OTEL_RESOURCE_ATTRIBUTES";

TEST(EnvironmentResourceDetectorTest, EnvVariableNotPresent) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  TestEnvironment::unsetEnvVar(kOtelResourceAttributesEnv);

  envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
      EnvironmentResourceDetectorConfig config;

  auto detector = std::make_unique<EnvironmentResourceDetector>(config, context);
  Resource resource = detector->detect();

  EXPECT_EQ(resource.schemaUrl_, "");
  EXPECT_TRUE(resource.attributes_.empty());
}

TEST(EnvironmentResourceDetectorTest, EnvVariablePresentButEmpty) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  TestEnvironment::setEnvVar(kOtelResourceAttributesEnv, "", 1);

  envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
      EnvironmentResourceDetectorConfig config;

  auto detector = std::make_unique<EnvironmentResourceDetector>(config, context);
  Resource resource = detector->detect();

  EXPECT_EQ(resource.schemaUrl_, "");
  EXPECT_TRUE(resource.attributes_.empty());
  TestEnvironment::unsetEnvVar(kOtelResourceAttributesEnv);
}

TEST(EnvironmentResourceDetectorTest, EnvVariablePresentAndWithAttributes) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  TestEnvironment::setEnvVar(kOtelResourceAttributesEnv, "key1=val1,key2=val2", 1);
  ResourceAttributes expected_attributes = {{"key1", "val1"}, {"key2", "val2"}};

  Api::ApiPtr api = Api::createApiForTest();
  EXPECT_CALL(context.server_factory_context_, api()).WillRepeatedly(ReturnRef(*api));

  envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
      EnvironmentResourceDetectorConfig config;

  auto detector = std::make_unique<EnvironmentResourceDetector>(config, context);
  Resource resource = detector->detect();

  EXPECT_EQ(resource.schemaUrl_, "");
  EXPECT_EQ(2, resource.attributes_.size());

  for (auto& actual : resource.attributes_) {
    auto expected = expected_attributes.find(actual.first);

    EXPECT_TRUE(expected != expected_attributes.end());
    EXPECT_EQ(expected->second, actual.second);
  }
  TestEnvironment::unsetEnvVar(kOtelResourceAttributesEnv);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
