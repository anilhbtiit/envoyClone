#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/dubbo_proxy.pb.validate.h"

#include "extensions/filters/network/dubbo_proxy/config.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/test_base.h"

#include "gmock/gmock.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

using DubboFilterConfigTest = TestBase;

TEST_F(DubboFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(DubboProxyFilterConfigFactory().createFilterFactoryFromProto(
                   envoy::config::filter::network::dubbo_proxy::v2alpha1::DubboProxy(), context),
               ProtoValidationException);
}

TEST_F(DubboFilterConfigTest, ValidProtoConfiguration) {
  envoy::config::filter::network::dubbo_proxy::v2alpha1::DubboProxy config{};

  config.set_stat_prefix("my_stat_prefix");

  NiceMock<Server::Configuration::MockFactoryContext> context;
  DubboProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

TEST_F(DubboFilterConfigTest, DubboProxyWithEmptyProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  DubboProxyFilterConfigFactory factory;
  envoy::config::filter::network::dubbo_proxy::v2alpha1::DubboProxy config =
      *dynamic_cast<envoy::config::filter::network::dubbo_proxy::v2alpha1::DubboProxy*>(
          factory.createEmptyConfigProto().get());
  config.set_stat_prefix("my_stat_prefix");

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
