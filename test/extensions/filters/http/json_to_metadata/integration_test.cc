#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

class JsonToMetadataIntegrationTest : public Event::TestUsingSimulatedTime,
                                      public HttpProtocolIntegrationTest {
public:
  void initializeFilter() {
    config_helper_.prependFilter(filter_config_);
    initialize();
  }

  void runTest(const Http::RequestHeaderMap& request_headers, const std::string& request_body,
               const size_t chunk_size = 5, bool has_trailer = false) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    IntegrationStreamDecoderPtr response;
    if (request_body.empty()) {
      response = codec_client_->makeHeaderOnlyRequest(request_headers);
    } else {
      auto encoder_decoder = codec_client_->startRequest(request_headers);
      request_encoder_ = &encoder_decoder.first;
      response = std::move(encoder_decoder.second);
      size_t i = 0;
      for (; i < request_body.length() / chunk_size; i++) {
        Buffer::OwnedImpl buffer(request_body.substr(i * chunk_size, chunk_size));
        codec_client_->sendData(*request_encoder_, buffer, false);
      }
      // Send the last chunk flagged as end_stream.
      Buffer::OwnedImpl buffer(
          request_body.substr(i * chunk_size, request_body.length() % chunk_size));
      codec_client_->sendData(*request_encoder_, buffer, !has_trailer);

      if (has_trailer) {
        codec_client_->sendTrailers(*request_encoder_, incoming_trailers_);
      }
    }
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());

    // cleanup
    codec_client_->close();
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  const std::string filter_config_ = R"EOF(
name: envoy.filters.http.json_to_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.json_to_metadata.v3.JsonToMetadata
  request_rules:
    rules:
    - selectors:
      - key: version
      on_present:
        metadata_namespace: envoy.lb
        key: version
      on_missing:
        metadata_namespace: envoy.lb
        key: version
        value: 'unknown'
        preserve_existing_metadata_value: true
      on_error:
        metadata_namespace: envoy.lb
        key: version
        value: 'error'
        preserve_existing_metadata_value: true
)EOF";

  const Http::TestRequestHeaderMapImpl incoming_headers_{{":scheme", "http"},
                                                         {":path", "/ping"},
                                                         {":method", "GET"},
                                                         {":authority", "host"},
                                                         {"Content-Type", "application/json"}};
  const Http::TestRequestTrailerMapImpl incoming_trailers_{{"request1", "trailer1"},
                                                           {"request2", "trailer2"}};

  std::string request_body_ =
      R"delimiter(
        {"version":"1.0.0",
        "messages":[
          {"role":"user","content":"content A"},
          {"role":"assistant","content":"content B"},
          {"role":"user","content":"content C"},
          {"role":"assistant","content":"content D"},
          {"role":"user","content":"content E"}],
        "stream":true})delimiter";
};

// TODO(#26236): Fix test suite for HTTP/3.
INSTANTIATE_TEST_SUITE_P(
    Protocols, JsonToMetadataIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(JsonToMetadataIntegrationTest, Basic) {
  initializeFilter();

  runTest(incoming_headers_, request_body_);

  EXPECT_EQ(1UL, test_server_->counter("json_to_metadata.rq_success")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_invalid_json_body")->value());
}

TEST_P(JsonToMetadataIntegrationTest, BasicOneChunk) {
  initializeFilter();

  runTest(incoming_headers_, request_body_, 1);

  EXPECT_EQ(1UL, test_server_->counter("json_to_metadata.rq_success")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_invalid_json_body")->value());
}

TEST_P(JsonToMetadataIntegrationTest, Trailer) {
  initializeFilter();

  runTest(incoming_headers_, request_body_, 5, true);

  EXPECT_EQ(1UL, test_server_->counter("json_to_metadata.rq_success")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_invalid_json_body")->value());
}

TEST_P(JsonToMetadataIntegrationTest, GoOverWaterMarkAtEndStream) {
  config_helper_.setBufferLimits(request_body_.length(), request_body_.length());
  initializeFilter();

  runTest(incoming_headers_, request_body_);

  EXPECT_EQ(1UL, test_server_->counter("json_to_metadata.rq_success")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_invalid_json_body")->value());
}

TEST_P(JsonToMetadataIntegrationTest, GoOverWaterMarkBeforeEndStream) {
  config_helper_.setBufferLimits(request_body_.length() / 2, request_body_.length() / 2);
  initializeFilter();

  runTest(incoming_headers_, request_body_);

  EXPECT_EQ(1UL, test_server_->counter("json_to_metadata.rq_success")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_invalid_json_body")->value());
}

TEST_P(JsonToMetadataIntegrationTest, MismatchedContentType) {
  initializeFilter();

  const Http::TestRequestHeaderMapImpl incoming_headers{{":scheme", "http"},
                                                        {":path", "/ping"},
                                                        {":method", "GET"},
                                                        {":authority", "host"},
                                                        {"Content-Type", "application/x-thrift"}};

  runTest(incoming_headers, request_body_);

  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_success")->value());
  EXPECT_EQ(1UL, test_server_->counter("json_to_metadata.rq_mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_invalid_json_body")->value());
}

TEST_P(JsonToMetadataIntegrationTest, NoBody) {
  initializeFilter();

  runTest(incoming_headers_, "");

  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_success")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_mismatched_content_type")->value());
  EXPECT_EQ(1UL, test_server_->counter("json_to_metadata.rq_no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_invalid_json_body")->value());
}

TEST_P(JsonToMetadataIntegrationTest, InvalidJson) {
  initializeFilter();

  runTest(incoming_headers_, "it's not a json body");

  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_success")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("json_to_metadata.rq_no_body")->value());
  EXPECT_EQ(1UL, test_server_->counter("json_to_metadata.rq_invalid_json_body")->value());
}

} // namespace
} // namespace Envoy
