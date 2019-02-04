#include "envoy/config/filter/http/transcoder/v2/transcoder.pb.validate.h"

#include "extensions/filters/http/grpc_json_transcoder/config.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/test_base.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

TEST(GrpcJsonTranscoderFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(
      GrpcJsonTranscoderFilterConfig().createFilterFactoryFromProto(
          envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder(), "stats", context),
      ProtoValidationException);
}

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
