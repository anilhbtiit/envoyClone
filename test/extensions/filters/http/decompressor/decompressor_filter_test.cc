#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.h"

#include "common/http/headers.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/decompressor/decompressor_filter.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/compression/decompressor/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::ByMove;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Decompressor {
namespace {

class DecompressorFilterTest : public testing::TestWithParam<bool> {
public:
  DecompressorFilterTest() {}

  void SetUp() override {
    setUpFilter(R"EOF(
decompressor_library:
  typed_config:
    "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
)EOF");
  }

  void setUpFilter(std::string&& yaml) {
    envoy::extensions::filters::http::decompressor::v3::Decompressor decompressor;
    TestUtility::loadFromYaml(yaml, decompressor);
    auto decompressor_factory =
        std::make_unique<NiceMock<Compression::Decompressor::MockDecompressorFactory>>();
    decompressor_factory_ = decompressor_factory.get();
    config_ = std::make_shared<DecompressorFilterConfig>(decompressor, "test.", stats_, runtime_,
                                                         std::move(decompressor_factory));
    filter_ = std::make_unique<DecompressorFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  bool requestDirection() { return GetParam(); }

  Compression::Decompressor::MockDecompressorFactory* decompressor_factory_{};
  DecompressorFilterConfigSharedPtr config_;
  std::unique_ptr<DecompressorFilter> filter_;
  Stats::TestUtil::TestStore stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

INSTANTIATE_TEST_SUITE_P(RequestOrResponse, DecompressorFilterTest, ::testing::Values(true, false));

TEST_P(DecompressorFilterTest, DecompressionActive) {
  // Keep the decompressor to set expectations about it
  auto decompressor = std::make_unique<Compression::Decompressor::MockDecompressor>();
  auto* decompressor_ptr = decompressor.get();
  EXPECT_CALL(*decompressor_factory_, createDecompressor())
      .WillOnce(Return(ByMove(std::move(decompressor))));

  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers;
  if (requestDirection()) {
    auto request_headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
        {"content-encoding", "mock"}, {"content-length", "256"}}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(*request_headers, false));
    headers = std::move(request_headers);
  } else {
    auto response_headers = Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{
        {"content-encoding", "mock"}, {"content-length", "256"}}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue,
              filter_->encodeHeaders(*response_headers, false));
    headers = std::move(response_headers);
  }

  EXPECT_EQ("br", headers->ContentEncoding()->value().getStringView());

  // FIX ME(junr03): pending decision on this.
  EXPECT_EQ(nullptr, headers->ContentLength());
  EXPECT_EQ("chunked", headers->TransferEncoding()->value().getStringView());

  EXPECT_CALL(*decompressor_ptr, decompress(_, _))
      .Times(2)
      .WillRepeatedly(
          Invoke([&](const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer) {
            TestUtility::feedBufferWithRandomCharacters(output_buffer, 2 * input_buffer.length());
          }));

  Buffer::OwnedImpl buffer;
  TestUtility::feedBufferWithRandomCharacters(buffer, 10);
  EXPECT_EQ(10, buffer.length());
  if (requestDirection()) {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
  } else {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));
  }
  EXPECT_EQ(20, buffer.length());
  if (requestDirection()) {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
  } else {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));
  }
  EXPECT_EQ(40, buffer.length());
}

TEST_P(DecompressorFilterTest, DecompressionActiveMultipleEncodings) {
  // Keep the decompressor to set expectations about it
  auto decompressor = std::make_unique<Compression::Decompressor::MockDecompressor>();
  auto* decompressor_ptr = decompressor.get();
  EXPECT_CALL(*decompressor_factory_, createDecompressor())
      .WillOnce(Return(ByMove(std::move(decompressor))));

  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers;
  if (requestDirection()) {
    auto request_headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
        {"content-encoding", "mock, br"}, {"content-length", "256"}}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(*request_headers, false));
    headers = std::move(request_headers);
  } else {
    auto response_headers = Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{
        {"content-encoding", "mock, br"}, {"content-length", "256"}}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue,
              filter_->encodeHeaders(*response_headers, false));
    headers = std::move(response_headers);
  }

  EXPECT_EQ("br", headers->ContentEncoding()->value().getStringView());

  // FIX ME(junr03): pending decision on this.
  EXPECT_EQ(nullptr, headers->ContentLength());
  EXPECT_EQ("chunked", headers->TransferEncoding()->value().getStringView());

  EXPECT_CALL(*decompressor_ptr, decompress(_, _))
      .Times(2)
      .WillRepeatedly(
          Invoke([&](const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer) {
            TestUtility::feedBufferWithRandomCharacters(output_buffer, 2 * input_buffer.length());
          }));

  Buffer::OwnedImpl buffer;
  TestUtility::feedBufferWithRandomCharacters(buffer, 10);
  EXPECT_EQ(10, buffer.length());
  if (requestDirection()) {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
  } else {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));
  }
  EXPECT_EQ(20, buffer.length());
  if (requestDirection()) {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
  } else {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));
  }
  EXPECT_EQ(40, buffer.length());
}

TEST_P(DecompressorFilterTest, DecompressionDisabled) {
  setUpFilter(R"EOF(
response_decompression_enabled:
  default_value: false
  runtime_key: does_not_exist
decompressor_library:
  typed_config:
    "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
)EOF");

  EXPECT_CALL(*decompressor_factory_, createDecompressor()).Times(0);
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers;
  if (requestDirection()) {
    auto request_headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
        {"content-encoding", "mock"}, {"content-length", "256"}}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(*request_headers, false));
    headers = std::move(request_headers);
  } else {
    auto response_headers = Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{
        {"content-encoding", "mock"}, {"content-length", "256"}}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue,
              filter_->encodeHeaders(*response_headers, false));
    headers = std::move(response_headers);
  }

  Buffer::OwnedImpl buffer;
  TestUtility::feedBufferWithRandomCharacters(buffer, 10);
  EXPECT_EQ(10, buffer.length());
  if (requestDirection()) {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
  } else {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
  }
  EXPECT_EQ(10, buffer.length());
}

TEST_P(DecompressorFilterTest, DecompressionContentEncodingDoesNotMatch) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor()).Times(0);

  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers;
  if (requestDirection()) {
    auto request_headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
        {"content-encoding", "not-matching"}, {"content-length", "256"}}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(*request_headers, false));
    headers = std::move(request_headers);
  } else {
    auto response_headers = Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{
        {"content-encoding", "not-matching"}, {"content-length", "256"}}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue,
              filter_->encodeHeaders(*response_headers, false));
    headers = std::move(response_headers);
  }

  Buffer::OwnedImpl buffer;
  TestUtility::feedBufferWithRandomCharacters(buffer, 10);
  EXPECT_EQ(10, buffer.length());
  if (requestDirection()) {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
  } else {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
  }
  EXPECT_EQ(10, buffer.length());
}

TEST_P(DecompressorFilterTest, DecompressionContentEncodingNotCurrent) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor()).Times(0);

  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers;
  if (requestDirection()) {
    auto request_headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
        {"content-encoding", "gzip,mock"}, {"content-length", "256"}}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(*request_headers, false));
    headers = std::move(request_headers);
  } else {
    auto response_headers = Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{
        {"content-encoding", "gzip,mock"}, {"content-length", "256"}}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue,
              filter_->encodeHeaders(*response_headers, false));
    headers = std::move(response_headers);
  }

  Buffer::OwnedImpl buffer;
  TestUtility::feedBufferWithRandomCharacters(buffer, 10);
  EXPECT_EQ(10, buffer.length());
  if (requestDirection()) {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
  } else {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
  }
  EXPECT_EQ(10, buffer.length());
}

TEST_P(DecompressorFilterTest, ResponseDecompressionNoTransformPresent) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor()).Times(0);

  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers;
  if (requestDirection()) {
    auto request_headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
        {"cache-control", Http::Headers::get().CacheControlValues.NoTransform},
        {"content-encoding", "mock"},
        {"content-length", "256"}}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(*request_headers, false));
    headers = std::move(request_headers);
  } else {
    auto response_headers = Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{
        {"cache-control", Http::Headers::get().CacheControlValues.NoTransform},
        {"content-encoding", "mock"},
        {"content-length", "256"}}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue,
              filter_->encodeHeaders(*response_headers, false));
    headers = std::move(response_headers);
  }

  Buffer::OwnedImpl buffer;
  TestUtility::feedBufferWithRandomCharacters(buffer, 10);
  EXPECT_EQ(10, buffer.length());
  if (requestDirection()) {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
  } else {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
  }
  EXPECT_EQ(10, buffer.length());
}

} // namespace
} // namespace Decompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
