#include "source/common/http/character_set_validation.h"
#include "source/extensions/http/header_validators/envoy_default/character_tables.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {

using DownstreamUhvIntegrationTest = HttpProtocolIntegrationTest;
INSTANTIATE_TEST_SUITE_P(Protocols, DownstreamUhvIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2,
                              Http::CodecType::HTTP3},
                             {Http::CodecType::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Without the `allow_non_compliant_characters_in_path` override UHV rejects requests with backslash
// in the path.
TEST_P(DownstreamUhvIntegrationTest, BackslashInUriPathConversionWithUhvOverride) {
  config_helper_.addRuntimeOverride("envoy.uhv.allow_non_compliant_characters_in_path", "false");
  disable_client_header_validation_ = true;
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path\\with%5Cback%5Cslashes"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
#ifdef ENVOY_ENABLE_UHV
  // By default Envoy disconnects connection on protocol errors
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  if (downstream_protocol_ != Http::CodecType::HTTP2) {
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else {
    ASSERT_TRUE(response->reset());
    EXPECT_EQ(Http::StreamResetReason::ConnectionTermination, response->resetReason());
  }
#else
  waitForNextUpstreamRequest();

  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%5Cback%5Cslashes");

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
#endif
}

// By default the `allow_non_compliant_characters_in_path` == true and UHV behaves just like legacy
// path normalization.
TEST_P(DownstreamUhvIntegrationTest, BackslashInUriPathConversion) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path\\with%5Cback%5Cslashes"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  waitForNextUpstreamRequest();

  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%5Cback%5Cslashes");

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// By default the `uhv_preserve_url_encoded_case` == true and UHV behaves just like legacy path
// normalization.
TEST_P(DownstreamUhvIntegrationTest, UrlEncodedTripletsCasePreserved) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path/with%3bmixed%5Ccase%Fesequences"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  waitForNextUpstreamRequest();

  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%3bmixed%5Ccase%Fesequences");

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// Without the `uhv_preserve_url_encoded_case` override UHV changes all percent encoded
// sequences to use uppercase characters.
TEST_P(DownstreamUhvIntegrationTest, UrlEncodedTripletsCasePreservedWithUhvOverride) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.uhv_preserve_url_encoded_case",
                                    "false");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path/with%3bmixed%5Ccase%Fesequences"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  waitForNextUpstreamRequest();

#ifdef ENVOY_ENABLE_UHV
  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%3Bmixed%5Ccase%FEsequences");
#else
  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%3bmixed%5Ccase%Fesequences");
#endif
  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
}

namespace {
std::string generateExtendedAsciiString() {
  std::string extended_ascii_string;
  for (uint32_t ascii = 0x80; ascii <= 0xff; ++ascii) {
    extended_ascii_string.push_back(static_cast<char>(ascii));
  }
  return extended_ascii_string;
}

std::map<char, std::string> generateExtendedAsciiPercentEncoding() {
  std::map<char, std::string> encoding;
  for (uint32_t ascii = 0x80; ascii <= 0xff; ++ascii) {
    encoding.insert(
        {static_cast<char>(ascii), fmt::format("%{:02X}", static_cast<unsigned char>(ascii))});
  }
  return encoding;
}
} // namespace

// This test shows validation of character sets in URL path for all codecs.
// It also shows that UHV in compatibility mode has the same validation.
TEST_P(DownstreamUhvIntegrationTest, CharacterValidationInPathWithoutPathNormalization) {
  // This allows sending NUL, CR and LF in headers without triggering ASSERTs in Envoy
  Http::HeaderStringValidator::disable_validation_for_tests_ = true;
  disable_client_header_validation_ = true;
  config_helper_.addRuntimeOverride("envoy.reloadable_features.validate_upstream_headers", "false");
  config_helper_.addRuntimeOverride("envoy.uhv.allow_non_compliant_characters_in_path", "true");
  initialize();
  // All codecs allow the following characters that are outside of RFC "<>[]^`{}\|
  std::string additionally_allowed_characters(R"--("<>[]^`{}\|)--");
  if (downstream_protocol_ == Http::CodecType::HTTP3) {
    // In addition H/3 allows TAB and SPACE in path
    additionally_allowed_characters += +"\t ";
  } else if (downstream_protocol_ == Http::CodecType::HTTP2) {
    if (GetParam().http2_implementation == Http2Impl::Oghttp2) {
      // In addition H/2 oghttp2 allows TAB and SPACE in path
      additionally_allowed_characters += +"\t ";
    }
    // Both nghttp2 and oghttp2 allow extended ASCII >= 0x80 in path
    additionally_allowed_characters += generateExtendedAsciiString();
  }

  std::vector<FakeStreamPtr> upstream_requests;
  for (uint32_t ascii = 0x0; ascii <= 0xFF; ++ascii) {
    if (ascii == '?' || ascii == '#') {
      // These characters will just cause path to be interpreted with query or fragment
      continue;
    } else if ((downstream_protocol_ == Http::CodecType::HTTP3 ||
                (downstream_protocol_ == Http::CodecType::HTTP2 &&
                 GetParam().http2_implementation == Http2Impl::Oghttp2)) &&
               ascii == 0) {
      // QUIC client does weird things when a header contains nul character
      // oghttp2 concatenates path values when 0 is in the path
      continue;
    } else if (downstream_protocol_ == Http::CodecType::HTTP1 && (ascii == '\r' || ascii == '\n')) {
      // \r and \n will produce invalid HTTP/1 request on the wire
      continue;
    }
    auto client = makeHttpConnection(lookupPort("http"));

    std::string path("/path/with/additional/characters");
    path[12] = static_cast<char>(ascii);
    Http::HeaderString invalid_value{};
    invalid_value.setCopyUnvalidatedForTestOnly(path);
    Http::TestRequestHeaderMapImpl headers{
        {":scheme", "https"}, {":authority", "envoy.com"}, {":method", "GET"}};
    headers.addViaMove(Http::HeaderString(absl::string_view(":path")), std::move(invalid_value));
    auto response = client->makeHeaderOnlyRequest(headers);

    // Workaround the case that nghttp2 fake upstream will reject TAB or SPACE in path that was
    // allowed by the H/3 downstream codec
    bool expect_upstream_reject = GetParam().http2_implementation == Http2Impl::Nghttp2 &&
                                  downstream_protocol_ == Http::CodecType::HTTP3 &&
                                  (ascii == 0x9 || ascii == 0x20);

    if (Http::testCharInTable(
            Extensions::Http::HeaderValidators::EnvoyDefault::kPathHeaderCharTable,
            static_cast<char>(ascii)) ||
        absl::StrContains(additionally_allowed_characters, static_cast<char>(ascii))) {
      if (expect_upstream_reject) {
        if (!fake_upstream_connection_) {
          waitForNextUpstreamConnection({0}, TestUtility::DefaultTimeout,
                                        fake_upstream_connection_);
          EXPECT_TRUE(fake_upstream_connection_->waitForDisconnect());
          fake_upstream_connection_.reset();
        }
        EXPECT_TRUE(response->waitForEndStream());
        EXPECT_EQ("503", response->headers().getStatusValue());
      } else {
        waitForNextUpstreamRequest();
        EXPECT_EQ(upstream_request_->headers().getPathValue(), path);
        // Send a headers only response.
        upstream_request_->encodeHeaders(default_response_headers_, true);
        ASSERT_TRUE(response->waitForEndStream());
        upstream_requests.emplace_back(std::move(upstream_request_));
      }
    } else {
      ASSERT_TRUE(client->waitForDisconnect());
      if (downstream_protocol_ == Http::CodecType::HTTP1) {
        EXPECT_EQ("400", response->headers().getStatusValue());
      } else {
        EXPECT_TRUE(response->reset());
      }
    }
    client->close();
  }
}

TEST_P(DownstreamUhvIntegrationTest, CharacterValidationInPathWithPathNormalization) {
  // This allows sending NUL, CR and LF in headers without triggering ASSERTs in Envoy
  Http::HeaderStringValidator::disable_validation_for_tests_ = true;
  disable_client_header_validation_ = true;
  config_helper_.addRuntimeOverride("envoy.reloadable_features.validate_upstream_headers", "false");
  config_helper_.addRuntimeOverride("envoy.uhv.allow_non_compliant_characters_in_path", "true");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  // All codecs allow the following characters that are outside of RFC "<>[]^`{}\|
  std::string additionally_allowed_characters(R"--("<>[]^`{}\|)--");
  if (downstream_protocol_ == Http::CodecType::HTTP3) {
    // In addition H/3 allows TAB and SPACE in path
    additionally_allowed_characters += +"\t ";
  } else if (downstream_protocol_ == Http::CodecType::HTTP2) {
    if (GetParam().http2_implementation == Http2Impl::Oghttp2) {
      // In addition H/2 oghttp2 allows TAB and SPACE in path
      additionally_allowed_characters += +"\t ";
    }
    // Both nghttp2 and oghttp2 allow extended ASCII >= 0x80 in path
    additionally_allowed_characters += generateExtendedAsciiString();
  }

  std::map<char, std::string> percent_encoded_characters{
      {'\t', "%09"}, {' ', "%20"}, {'"', "%22"}, {'<', "%3C"}, {'>', "%3E"}, {'\\', "/"},
      {'^', "%5E"},  {'`', "%60"}, {'{', "%7B"}, {'|', "%7C"}, {'}', "%7D"}};
  std::map<char, std::string> percent_encoded_extende_ascii =
      generateExtendedAsciiPercentEncoding();
  percent_encoded_characters.merge(percent_encoded_extende_ascii);
  std::vector<FakeStreamPtr> upstream_requests;
  for (uint32_t ascii = 0x0; ascii <= 0xFF; ++ascii) {
    if (ascii == '?' || ascii == '#') {
      // These characters will just cause path to be interpreted with query or fragment
      continue;
    } else if ((downstream_protocol_ == Http::CodecType::HTTP3 ||
                (downstream_protocol_ == Http::CodecType::HTTP2 &&
                 GetParam().http2_implementation == Http2Impl::Oghttp2)) &&
               ascii == 0) {
      // QUIC client does weird things when a header contains nul character
      // oghttp2 concatenates path values when 0 is in the path
      continue;
    } else if (downstream_protocol_ == Http::CodecType::HTTP1 && (ascii == '\r' || ascii == '\n')) {
      // \r and \n will produce invalid HTTP/1 request on the wire
      continue;
    }
    auto client = makeHttpConnection(lookupPort("http"));

    std::string path("/path/with/additional/characters");
    path[12] = static_cast<char>(ascii);
    Http::HeaderString invalid_value{};
    invalid_value.setCopyUnvalidatedForTestOnly(path);
    Http::TestRequestHeaderMapImpl headers{
        {":scheme", "https"}, {":authority", "envoy.com"}, {":method", "GET"}};
    headers.addViaMove(Http::HeaderString(absl::string_view(":path")), std::move(invalid_value));
    auto response = client->makeHeaderOnlyRequest(headers);

    if (Http::testCharInTable(
            Extensions::Http::HeaderValidators::EnvoyDefault::kPathHeaderCharTable,
            static_cast<char>(ascii)) ||
        absl::StrContains(additionally_allowed_characters, static_cast<char>(ascii))) {
      waitForNextUpstreamRequest();
      auto encoding = percent_encoded_characters.find(static_cast<char>(ascii));
      if (encoding != percent_encoded_characters.end()) {
        path = absl::StrCat("/path/with/a", encoding->second, "ditional/characters");
      }
      EXPECT_EQ(upstream_request_->headers().getPathValue(), path);
      // Send a headers only response.
      upstream_request_->encodeHeaders(default_response_headers_, true);
      ASSERT_TRUE(response->waitForEndStream());
      upstream_requests.emplace_back(std::move(upstream_request_));
    } else {
      ASSERT_TRUE(client->waitForDisconnect());
      if (downstream_protocol_ == Http::CodecType::HTTP1) {
        EXPECT_EQ("400", response->headers().getStatusValue());
      } else {
        EXPECT_TRUE(response->reset());
      }
    }
    client->close();
  }
}

TEST_P(DownstreamUhvIntegrationTest, CharacterValidationInQuery) {
  // This allows sending NUL, CR and LF in headers without triggering ASSERTs in Envoy
  Http::HeaderStringValidator::disable_validation_for_tests_ = true;
  disable_client_header_validation_ = true;
  config_helper_.addRuntimeOverride("envoy.reloadable_features.validate_upstream_headers", "false");
  config_helper_.addRuntimeOverride("envoy.uhv.allow_non_compliant_characters_in_path", "true");
  // Path normalization should not affect query, however enable it to make sure it is so.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  // All codecs allow the following characters that are outside of RFC "<>[]^`{}\|
  std::string additionally_allowed_characters(R"--("<>[]^`{}\|)--");
  if (downstream_protocol_ == Http::CodecType::HTTP3) {
    // In addition H/3 allows TAB and SPACE in path
    additionally_allowed_characters += +"\t ";
  } else if (downstream_protocol_ == Http::CodecType::HTTP2) {
    if (GetParam().http2_implementation == Http2Impl::Oghttp2) {
      // In addition H/2 oghttp2 allows TAB and SPACE in path
      additionally_allowed_characters += +"\t ";
    }
    // Both nghttp2 and oghttp2 allow extended ASCII >= 0x80 in path
    additionally_allowed_characters += generateExtendedAsciiString();
  }

  std::vector<FakeStreamPtr> upstream_requests;
  for (uint32_t ascii = 0x0; ascii <= 0xFF; ++ascii) {
    if (ascii == '#') {
      // This character will just cause path to be interpreted as having a fragment
      continue;
    } else if ((downstream_protocol_ == Http::CodecType::HTTP3 ||
                (downstream_protocol_ == Http::CodecType::HTTP2 &&
                 GetParam().http2_implementation == Http2Impl::Oghttp2)) &&
               ascii == 0) {
      // QUIC client does weird things when a header contains nul character
      // oghttp2 concatenates path values when 0 is in the path
      continue;
    } else if (downstream_protocol_ == Http::CodecType::HTTP1 && (ascii == '\r' || ascii == '\n')) {
      // \r and \n will produce invalid HTTP/1 request on the wire
      continue;
    }
    auto client = makeHttpConnection(lookupPort("http"));

    std::string path("/query?with=additional&characters");
    path[12] = static_cast<char>(ascii);
    Http::HeaderString invalid_value{};
    invalid_value.setCopyUnvalidatedForTestOnly(path);
    Http::TestRequestHeaderMapImpl headers{
        {":scheme", "https"}, {":authority", "envoy.com"}, {":method", "GET"}};
    headers.addViaMove(Http::HeaderString(absl::string_view(":path")), std::move(invalid_value));
    auto response = client->makeHeaderOnlyRequest(headers);

    // Workaround the case that nghttp2 fake upstream will reject TAB or SPACE in path that was
    // allowed by the H/3 downstream codec
    bool expect_upstream_reject = GetParam().http2_implementation == Http2Impl::Nghttp2 &&
                                  downstream_protocol_ == Http::CodecType::HTTP3 &&
                                  (ascii == 0x9 || ascii == 0x20);

    if (Http::testCharInTable(Http::kUriQueryAndFragmentCharTable, static_cast<char>(ascii)) ||
        absl::StrContains(additionally_allowed_characters, static_cast<char>(ascii))) {
      if (expect_upstream_reject) {
        if (!fake_upstream_connection_) {
          waitForNextUpstreamConnection({0}, TestUtility::DefaultTimeout,
                                        fake_upstream_connection_);
          EXPECT_TRUE(fake_upstream_connection_->waitForDisconnect());
          fake_upstream_connection_.reset();
        }
        EXPECT_TRUE(response->waitForEndStream());
        EXPECT_EQ("503", response->headers().getStatusValue());
      } else {
        waitForNextUpstreamRequest();
        EXPECT_EQ(upstream_request_->headers().getPathValue(), path);
        // Send a headers only response.
        upstream_request_->encodeHeaders(default_response_headers_, true);
        ASSERT_TRUE(response->waitForEndStream());
        upstream_requests.emplace_back(std::move(upstream_request_));
      }
    } else {
      ASSERT_TRUE(client->waitForDisconnect());
      if (downstream_protocol_ == Http::CodecType::HTTP1) {
        EXPECT_EQ("400", response->headers().getStatusValue());
      } else {
        EXPECT_TRUE(response->reset());
      }
    }
    client->close();
  }
}

TEST_P(DownstreamUhvIntegrationTest, CharacterValidationInFragment) {
  // This allows sending NUL, CR and LF in headers without triggering ASSERTs in Envoy
  Http::HeaderStringValidator::disable_validation_for_tests_ = true;
  disable_client_header_validation_ = true;
  config_helper_.addRuntimeOverride("envoy.reloadable_features.validate_upstream_headers", "false");
  config_helper_.addRuntimeOverride("envoy.uhv.allow_non_compliant_characters_in_path", "true");
  // By default path with fragment is rejected, disable it for the test
  config_helper_.addRuntimeOverride("envoy.reloadable_features.http_reject_path_with_fragment",
                                    "false");
  initialize();
  // All codecs allow the following characters that are outside of RFC "<>[]^`{}\|#
  std::string additionally_allowed_characters(R"--("<>[]^`{}\|#)--");
  if (downstream_protocol_ == Http::CodecType::HTTP3) {
    // In addition H/3 allows TAB and SPACE in path
    additionally_allowed_characters += +"\t ";
  } else if (downstream_protocol_ == Http::CodecType::HTTP2) {
    if (GetParam().http2_implementation == Http2Impl::Oghttp2) {
      // In addition H/2 oghttp2 allows TAB and SPACE in path
      additionally_allowed_characters += +"\t ";
    }
    // Both nghttp2 and oghttp2 allow extended ASCII >= 0x80 in path
    additionally_allowed_characters += generateExtendedAsciiString();
  }

  std::vector<FakeStreamPtr> upstream_requests;
  for (uint32_t ascii = 0x0; ascii <= 0xFF; ++ascii) {
    if ((downstream_protocol_ == Http::CodecType::HTTP3 ||
         (downstream_protocol_ == Http::CodecType::HTTP2 &&
          GetParam().http2_implementation == Http2Impl::Oghttp2)) &&
        ascii == 0) {
      // QUIC client does weird things when a header contains nul character
      // oghttp2 concatenates path values when 0 is in the path
      continue;
    } else if (downstream_protocol_ == Http::CodecType::HTTP1 && (ascii == '\r' || ascii == '\n')) {
      // \r and \n will produce invalid HTTP/1 request on the wire
      continue;
    }
    auto client = makeHttpConnection(lookupPort("http"));

    std::cout << "Sending " << ascii << std::endl;
    std::string path("/q?with=a#fragment");
    path[12] = static_cast<char>(ascii);
    Http::HeaderString invalid_value{};
    invalid_value.setCopyUnvalidatedForTestOnly(path);
    Http::TestRequestHeaderMapImpl headers{
        {":scheme", "https"}, {":authority", "envoy.com"}, {":method", "GET"}};
    headers.addViaMove(Http::HeaderString(absl::string_view(":path")), std::move(invalid_value));
    auto response = client->makeHeaderOnlyRequest(headers);

    if (Http::testCharInTable(Http::kUriQueryAndFragmentCharTable, static_cast<char>(ascii)) ||
        absl::StrContains(additionally_allowed_characters, static_cast<char>(ascii))) {
      waitForNextUpstreamRequest();
      EXPECT_EQ(upstream_request_->headers().getPathValue(), "/q?with=a");
      // Send a headers only response.
      upstream_request_->encodeHeaders(default_response_headers_, true);
      ASSERT_TRUE(response->waitForEndStream());
      upstream_requests.emplace_back(std::move(upstream_request_));
    } else {
      ASSERT_TRUE(client->waitForDisconnect());
      if (downstream_protocol_ == Http::CodecType::HTTP1) {
        EXPECT_EQ("400", response->headers().getStatusValue());
      } else {
        EXPECT_TRUE(response->reset());
      }
    }
    client->close();
  }
}

// Without the `uhv_allow_malformed_url_encoding` override UHV rejects requests with malformed
// percent encoding.
TEST_P(DownstreamUhvIntegrationTest, MalformedUrlEncodedTripletsRejectedWithUhvOverride) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.uhv_allow_malformed_url_encoding",
                                    "false");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path%Z%30with%XYbad%7Jencoding%A"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
#ifdef ENVOY_ENABLE_UHV
  // By default Envoy disconnects connection on protocol errors
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  if (downstream_protocol_ != Http::CodecType::HTTP2) {
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else {
    ASSERT_TRUE(response->reset());
    EXPECT_EQ(Http::StreamResetReason::ConnectionTermination, response->resetReason());
  }
#else
  waitForNextUpstreamRequest();

  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path%Z0with%XYbad%7Jencoding%A");

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
#endif
}

// By default the `uhv_allow_malformed_url_encoding` == true and UHV behaves just like legacy path
// normalization.
TEST_P(DownstreamUhvIntegrationTest, MalformedUrlEncodedTripletsAllowed) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path%Z%30with%XYbad%7Jencoding%"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  waitForNextUpstreamRequest();

  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path%Z0with%XYbad%7Jencoding%");

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
}

} // namespace Envoy
