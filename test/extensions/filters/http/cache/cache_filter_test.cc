#include "source/extensions/filters/http/cache/simple_http_cache/config.pb.h"

#include "extensions/filters/http/cache/cache_filter.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

class CacheFilterTest : public ::testing::Test {
protected:
  CacheFilterTest() {
    config_.mutable_typed_config()->PackFrom(
        envoy::source::extensions::filters::http::cache::SimpleHttpCacheConfig());
    ASSERT(config_.typed_config()
               .Is<envoy::source::extensions::filters::http::cache::SimpleHttpCacheConfig>());
  }

  CacheFilter makeFilter() {
    CacheFilter filter(config_, /*stats_prefix=*/"", context_.scope(), context_.timeSource());
    filter.setDecoderFilterCallbacks(decoder_callbacks_);
    filter.setEncoderFilterCallbacks(encoder_callbacks_);
    return filter;
  }

  envoy::extensions::filters::http::cache::v3alpha::CacheConfig config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Event::SimulatedTimeSystem time_source_;
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  Http::TestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-forwarded-proto", "https"}};
  Http::TestHeaderMapImpl response_headers_{{":status", "200"},
                                            {"date", formatter_.now(time_source_)},
                                            {"cache-control", "public,max-age=3600"}};
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

TEST_F(CacheFilterTest, ImmediateHitNoBody) {
  request_headers_.setHost("ImmediateHitNoBody");
  ON_CALL(decoder_callbacks_, dispatcher()).WillByDefault(ReturnRef(context_.dispatcher_));
  ON_CALL(context_.dispatcher_, post(_)).WillByDefault(::testing::InvokeArgument<0>());

  {
    // Create filter for request 1
    CacheFilter filter = makeFilter();

    // Decode request 1 header
    EXPECT_CALL(decoder_callbacks_, continueDecoding);
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopIteration);
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode response header
    EXPECT_EQ(filter.encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
    filter.onDestroy();
  }
  {
    // Create filter for request 2
    CacheFilter filter = makeFilter();

    // Decode request 2 header
    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(testing::AllOf(IsSupersetOfHeaders(response_headers_),
                                              HeaderHasValueRef("age", "0")),
                               true));
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopIteration);
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
    filter.onDestroy();
  }
}

TEST_F(CacheFilterTest, ImmediateHitBody) {
  request_headers_.setHost("ImmediateHitBody");
  ON_CALL(decoder_callbacks_, dispatcher()).WillByDefault(ReturnRef(context_.dispatcher_));
  ON_CALL(context_.dispatcher_, post(_)).WillByDefault(::testing::InvokeArgument<0>());
  const std::string body = "abc";

  {
    // Create filter for request 1
    CacheFilter filter = makeFilter();

    // Decode request 1 header
    EXPECT_CALL(decoder_callbacks_, continueDecoding);
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopIteration);
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode response header
    Buffer::OwnedImpl buffer(body);
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter.encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter.encodeData(buffer, true), Http::FilterDataStatus::Continue);
    filter.onDestroy();
  }
  {
    // Create filter for request 2
    CacheFilter filter = makeFilter();

    // Decode request 2 header
    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(testing::AllOf(IsSupersetOfHeaders(response_headers_),
                                              HeaderHasValueRef("age", "0")),
                               false));
    EXPECT_CALL(decoder_callbacks_,
                encodeData(testing::Property(&Buffer::Instance::toString, testing::Eq(body)), true));
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopIteration);
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
    filter.onDestroy();
  }
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
