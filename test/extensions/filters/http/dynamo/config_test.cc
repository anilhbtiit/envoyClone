#include "envoy/config/filter/http/dynamo/v2/dynamo.pb.h"
#include "envoy/config/filter/http/dynamo/v2/dynamo.pb.validate.h"

#include "extensions/filters/http/dynamo/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {
namespace {

TEST(DynamoFilterConfigTest, DynamoFilter) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  DynamoFilterConfig factory;
  envoy::config::filter::http::dynamo::v2::Dynamo proto_config;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
