#include <memory>

#include "envoy/http/metadata_interface.h"

#include "extensions/filters/http/composite/filter.h"

#include "test/mocks/http/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {
namespace {

class FilterTest : public ::testing::Test {
public:
  FilterTest() {
    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    filter_.setEncoderFilterCallbacks(encoder_callbacks_);
  }

  // Templated since MockStreamFilter and MockStreamDecoder filter doesn't share a mock base class.
  template <class T> void expectDelegatedDecoding(T& filter_mock) {
    EXPECT_CALL(filter_mock, decodeHeaders(HeaderMapEqualRef(&default_request_headers_), false));
    EXPECT_CALL(filter_mock, decodeMetadata(_));
    EXPECT_CALL(filter_mock, decodeData(_, false));
    EXPECT_CALL(filter_mock, decodeTrailers(HeaderMapEqualRef(&default_request_trailers_)));
    EXPECT_CALL(filter_mock, decodeComplete());
  }

  // Templated since MockStreamFilter and MockStreamEncoder filter doesn't share a mock base class.
  template <class T> void expectDelegatedEncoding(T& filter_mock) {
    EXPECT_CALL(filter_mock,
                encode100ContinueHeaders(HeaderMapEqualRef(&default_response_headers_)));
    EXPECT_CALL(filter_mock, encodeHeaders(HeaderMapEqualRef(&default_response_headers_), false));
    EXPECT_CALL(filter_mock, encodeMetadata(_));
    EXPECT_CALL(filter_mock, encodeData(_, false));
    EXPECT_CALL(filter_mock, encodeTrailers(HeaderMapEqualRef(&default_response_trailers_)));
    EXPECT_CALL(filter_mock, encodeComplete());
  }

  void doAllDecodingCallbacks() {
    filter_.decodeHeaders(default_request_headers_, false);

    Http::MetadataMap metadata;
    filter_.decodeMetadata(metadata);

    Buffer::OwnedImpl buffer("data");
    filter_.decodeData(buffer, false);

    filter_.decodeTrailers(default_request_trailers_);

    filter_.decodeComplete();
  }

  void doAllEncodingCallbacks() {
    filter_.encode100ContinueHeaders(default_response_headers_);

    filter_.encodeHeaders(default_response_headers_, false);

    Http::MetadataMap metadata;
    filter_.encodeMetadata(metadata);

    Buffer::OwnedImpl buffer("data");
    filter_.encodeData(buffer, false);

    filter_.encodeTrailers(default_response_trailers_);

    filter_.encodeComplete();
  }

  Http::MockStreamDecoderFilterCallbacks decoder_callbacks_;
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks_;
  Filter filter_;

  Http::TestRequestHeaderMapImpl default_request_headers_{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  Http::TestRequestTrailerMapImpl default_request_trailers_{{"trailers", "something"}};
  Http::TestResponseHeaderMapImpl default_response_headers_{{":status", "200"}};
  Http::TestResponseTrailerMapImpl default_response_trailers_{
      {"response-trailer", "something-else"}};
};

TEST_F(FilterTest, StreamEncoderFilterDelegation) {
  auto stream_filter = std::make_shared<Http::MockStreamEncoderFilter>();

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamEncoderFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setEncoderFilterCallbacks(_));
  CompositeAction action(factory_callback);
  filter_.onMatchCallback(action);

  doAllDecodingCallbacks();
  expectDelegatedEncoding(*stream_filter);
  doAllEncodingCallbacks();
  EXPECT_CALL(*stream_filter, onDestroy());
  filter_.onDestroy();
}

TEST_F(FilterTest, StreamDecoderFilterDelegation) {
  auto stream_filter = std::make_shared<Http::MockStreamDecoderFilter>();

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamDecoderFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setDecoderFilterCallbacks(_));
  CompositeAction action(factory_callback);
  filter_.onMatchCallback(action);

  expectDelegatedDecoding(*stream_filter);
  doAllDecodingCallbacks();
  doAllEncodingCallbacks();
  EXPECT_CALL(*stream_filter, onDestroy());
  filter_.onDestroy();
}

TEST_F(FilterTest, StreamFilterDelegation) {
  auto stream_filter = std::make_shared<Http::MockStreamFilter>();

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*stream_filter, setEncoderFilterCallbacks(_));
  CompositeAction action(factory_callback);
  filter_.onMatchCallback(action);

  expectDelegatedDecoding(*stream_filter);
  doAllDecodingCallbacks();
  expectDelegatedEncoding(*stream_filter);
  doAllEncodingCallbacks();
  EXPECT_CALL(*stream_filter, onDestroy());
  filter_.onDestroy();
}

} // namespace
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy