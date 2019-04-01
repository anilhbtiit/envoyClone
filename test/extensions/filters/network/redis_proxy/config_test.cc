#include "envoy/config/filter/network/redis_proxy/v2/redis_proxy.pb.validate.h"

#include "extensions/filters/network/redis_proxy/config.h"

#include "common/protobuf/utility.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

TEST(RedisProxyFilterConfigFactoryTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(RedisProxyFilterConfigFactory().createFilterFactoryFromProto(
                   envoy::config::filter::network::redis_proxy::v2::RedisProxy(), context),
               ProtoValidationException);
}

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyNoSettings) {
  const std::string yaml = R"EOF(
cluster: fake_cluster
stat_prefix: foo
  )EOF";

  envoy::config::filter::network::redis_proxy::v2::RedisProxy proto_config;
  EXPECT_THROW_WITH_REGEX(MessageUtil::loadFromYamlAndValidate(yaml, proto_config), ProtoValidationException, "value is required");
}

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyNoOpTimeout) {
  const std::string yaml = R"EOF(
cluster: fake_cluster
stat_prefix: foo
settings: {}
  )EOF";

  envoy::config::filter::network::redis_proxy::v2::RedisProxy proto_config;
  EXPECT_THROW_WITH_REGEX(MessageUtil::loadFromYamlAndValidate(yaml, proto_config), ProtoValidationException, "embedded message failed validation");
}

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyCorrectProto) {
  const std::string yaml = R"EOF(
cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.02s
  )EOF";

  envoy::config::filter::network::redis_proxy::v2::RedisProxy proto_config{};
  MessageUtil::loadFromYamlAndValidate(yaml, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyEmptyProto) {
  const std::string yaml = R"EOF(
cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.02s
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  envoy::config::filter::network::redis_proxy::v2::RedisProxy proto_config =
      *dynamic_cast<envoy::config::filter::network::redis_proxy::v2::RedisProxy*>(
          factory.createEmptyConfigProto().get());

  MessageUtil::loadFromYamlAndValidate(yaml, proto_config);

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
