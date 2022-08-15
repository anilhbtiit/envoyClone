#include "source/extensions/http/header_validators/envoy_default/header_validator.h"
#include "source/extensions/http/header_validators/envoy_default/path_normalizer.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

class PathNormalizerTest : public testing::Test {
protected:
  PathNormalizerPtr create(absl::string_view config_yaml) {
    envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);
    return std::make_unique<PathNormalizer>(typed_config);
  }

  static constexpr absl::string_view empty_config = "{}";
  static constexpr absl::string_view impl_specific_slash_handling_config = R"EOF(
    uri_path_normalization_options:
      path_with_escaped_slashes_action: IMPLEMENTATION_SPECIFIC_DEFAULT
    )EOF";
  static constexpr absl::string_view keep_encoded_slash_config = R"EOF(
    uri_path_normalization_options:
      path_with_escaped_slashes_action: KEEP_UNCHANGED
    )EOF";
  static constexpr absl::string_view reject_encoded_slash_config = R"EOF(
    uri_path_normalization_options:
      path_with_escaped_slashes_action: REJECT_REQUEST
    )EOF";
  static constexpr absl::string_view redirect_encoded_slash_config = R"EOF(
    uri_path_normalization_options:
      path_with_escaped_slashes_action: UNESCAPE_AND_REDIRECT
    )EOF";
  static constexpr absl::string_view decode_encoded_slash_config = R"EOF(
    uri_path_normalization_options:
      path_with_escaped_slashes_action: UNESCAPE_AND_FORWARD
    )EOF";
  static constexpr absl::string_view skip_merging_slashes_config = R"EOF(
    uri_path_normalization_options:
      skip_merging_slashes: true
    )EOF";
  static constexpr absl::string_view skip_merging_slashes_with_decode_slashes_config = R"EOF(
    uri_path_normalization_options:
      path_with_escaped_slashes_action: UNESCAPE_AND_FORWARD
      skip_merging_slashes: true
    )EOF";
};

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetDecoded) {
  char valid[] = "%7eX";

  auto normalizer = create(empty_config);
  auto decoded = normalizer->normalizeAndDecodeOctet(valid);

  EXPECT_EQ(decoded.result(), PathNormalizer::PercentDecodeResult::Decoded);
  EXPECT_EQ(decoded.octet(), '~');
  EXPECT_STREQ(valid, "%7EX");
}

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetNormalized) {
  char valid[] = "%ffX";

  auto normalizer = create(empty_config);

  EXPECT_EQ(normalizer->normalizeAndDecodeOctet(valid).result(),
            PathNormalizer::PercentDecodeResult::Normalized);
  EXPECT_STREQ(valid, "%FFX");
}

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetInvalid) {
  char invalid_length[] = "%";
  char invalid_length_2[] = "%a";
  char invalid_hex[] = "%ax";

  auto normalizer = create(empty_config);

  EXPECT_EQ(normalizer->normalizeAndDecodeOctet(invalid_length).result(),
            PathNormalizer::PercentDecodeResult::Invalid);
  EXPECT_EQ(normalizer->normalizeAndDecodeOctet(invalid_length_2).result(),
            PathNormalizer::PercentDecodeResult::Invalid);
  EXPECT_EQ(normalizer->normalizeAndDecodeOctet(invalid_hex).result(),
            PathNormalizer::PercentDecodeResult::Invalid);
}

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetKeepPathSepNotSet) {
  char valid[] = "%2fx";
  char win_valid[] = "%5cx";

  auto normalizer = create(empty_config);
  auto decoded = normalizer->normalizeAndDecodeOctet(valid);

  EXPECT_EQ(decoded.result(), PathNormalizer::PercentDecodeResult::Normalized);
  EXPECT_STREQ(valid, "%2Fx");

  auto win_decoded = normalizer->normalizeAndDecodeOctet(win_valid);
  EXPECT_EQ(win_decoded.result(), PathNormalizer::PercentDecodeResult::Normalized);
  EXPECT_STREQ(win_valid, "%5Cx");
}

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetKeepPathSepImplDefault) {
  char valid[] = "%2fx";
  char win_valid[] = "%5cx";

  auto normalizer = create(impl_specific_slash_handling_config);
  auto decoded = normalizer->normalizeAndDecodeOctet(valid);

  EXPECT_EQ(decoded.result(), PathNormalizer::PercentDecodeResult::Normalized);
  EXPECT_STREQ(valid, "%2Fx");

  auto win_decoded = normalizer->normalizeAndDecodeOctet(win_valid);
  EXPECT_EQ(win_decoded.result(), PathNormalizer::PercentDecodeResult::Normalized);
  EXPECT_STREQ(win_valid, "%5Cx");
}

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetKeepPathSepUnchanged) {
  char valid[] = "%2fx";
  char win_valid[] = "%5cx";

  auto normalizer = create(keep_encoded_slash_config);
  auto decoded = normalizer->normalizeAndDecodeOctet(valid);

  EXPECT_EQ(decoded.result(), PathNormalizer::PercentDecodeResult::Normalized);
  EXPECT_STREQ(valid, "%2Fx");

  auto win_decoded = normalizer->normalizeAndDecodeOctet(win_valid);
  EXPECT_EQ(win_decoded.result(), PathNormalizer::PercentDecodeResult::Normalized);
  EXPECT_STREQ(win_valid, "%5Cx");
}

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetRejectEncodedSlash) {
  char valid[] = "%2fx";
  char win_valid[] = "%5cx";

  auto normalizer = create(reject_encoded_slash_config);
  auto decoded = normalizer->normalizeAndDecodeOctet(valid);

  EXPECT_EQ(decoded.result(), PathNormalizer::PercentDecodeResult::Reject);
  EXPECT_STREQ(valid, "%2Fx");

  auto win_decoded = normalizer->normalizeAndDecodeOctet(win_valid);
  EXPECT_EQ(win_decoded.result(), PathNormalizer::PercentDecodeResult::Reject);
  EXPECT_STREQ(win_valid, "%5Cx");
}

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetRedirectEncodedSlash) {
  char valid[] = "%2fx";
  char win_valid[] = "%5cx";

  auto normalizer = create(redirect_encoded_slash_config);
  auto decoded = normalizer->normalizeAndDecodeOctet(valid);

  EXPECT_EQ(decoded.result(), PathNormalizer::PercentDecodeResult::DecodedRedirect);
  EXPECT_STREQ(valid, "%2Fx");
  EXPECT_EQ(decoded.octet(), '/');

  auto win_decoded = normalizer->normalizeAndDecodeOctet(win_valid);
  EXPECT_EQ(win_decoded.result(), PathNormalizer::PercentDecodeResult::DecodedRedirect);
  EXPECT_STREQ(win_valid, "%5Cx");
  EXPECT_EQ(win_decoded.octet(), '\\');
}

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetDecodedEncodedSlash) {
  char valid[] = "%2fx";
  char win_valid[] = "%5cx";

  auto normalizer = create(decode_encoded_slash_config);
  auto decoded = normalizer->normalizeAndDecodeOctet(valid);

  EXPECT_EQ(decoded.result(), PathNormalizer::PercentDecodeResult::Decoded);
  EXPECT_STREQ(valid, "%2Fx");
  EXPECT_EQ(decoded.octet(), '/');

  auto win_decoded = normalizer->normalizeAndDecodeOctet(win_valid);
  EXPECT_EQ(win_decoded.result(), PathNormalizer::PercentDecodeResult::Decoded);
  EXPECT_STREQ(win_valid, "%5Cx");
  EXPECT_EQ(win_decoded.octet(), '\\');
}

TEST_F(PathNormalizerTest, NormalizePathUriRoot) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriDotDot) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1/../dir2"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/dir2");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriDot) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1/./dir2"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/dir1/dir2");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriTrailingDotDot) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1/.."}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriTrailingDot) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1/."}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/dir1/");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriDotInSegments) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1/.dir2/..dir3/dir.4/dir..5"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/dir1/.dir2/..dir3/dir.4/dir..5");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriMergeSlashes) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "////root///child//"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/root/child/");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriPercentDecodeNormalized) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/%ff"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/%FF");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriPercentDecoded) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/%7e/dir1"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/~/dir1");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriSkipMergingSlashes) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "///root//child//"}};

  auto normalizer = create(skip_merging_slashes_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "///root//child//");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriSkipMergingSlashesWithDecodeSlashes) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "///root%2f/child/%2f"}};

  auto normalizer = create(skip_merging_slashes_with_decode_slashes_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "///root//child//");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriDecodeSlashes) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1%2fdir2%2f/dir3"}};

  auto normalizer = create(decode_encoded_slash_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/dir1/dir2/dir3");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriRejectEncodedSlashes) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1%2fdir2"}};

  auto normalizer = create(reject_encoded_slash_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(result.action(), HeaderValidator::RejectOrRedirectAction::Reject);
  EXPECT_EQ(result.details(), UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(PathNormalizerTest, NormalizePathUriRedirectEncodedSlashes) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1%2fdir2"}};

  auto normalizer = create(redirect_encoded_slash_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(result.action(), HeaderValidator::RejectOrRedirectAction::Redirect);
  EXPECT_EQ(result.details(), "uhv.path_noramlization_redirect");
  EXPECT_EQ(headers.path(), "/dir1/dir2");
}

TEST_F(PathNormalizerTest, NormalizePathUriNormalizeEncodedSlashesDefault) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1%2fdir2"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(headers.path(), "/dir1%2Fdir2");
}

TEST_F(PathNormalizerTest, NormalizePathUriNormalizeEncodedSlashesKeep) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1%2fdir2"}};

  auto normalizer = create(keep_encoded_slash_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(headers.path(), "/dir1%2Fdir2");
}

TEST_F(PathNormalizerTest, NormalizePathUriNormalizeEncodedSlashesImplDefault) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1%2fdir2"}};

  auto normalizer = create(impl_specific_slash_handling_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(headers.path(), "/dir1%2Fdir2");
}

TEST_F(PathNormalizerTest, NormalizePathUriInvalidBeyondRoot) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/.."}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(result.action(), HeaderValidator::RejectOrRedirectAction::Reject);
  EXPECT_EQ(result.details(), UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(PathNormalizerTest, NormalizePathUriInvalidRelative) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "./"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(result.action(), HeaderValidator::RejectOrRedirectAction::Reject);
  EXPECT_EQ(result.details(), UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(PathNormalizerTest, NormalizePathUriInvalidCharacter) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1\x7f"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(result.action(), HeaderValidator::RejectOrRedirectAction::Reject);
  EXPECT_EQ(result.details(), UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(PathNormalizerTest, NormalizePathUriInvalidEncoding) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/%x"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(result.action(), HeaderValidator::RejectOrRedirectAction::Reject);
  EXPECT_EQ(result.details(), UhvResponseCodeDetail::get().InvalidUrl);
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
