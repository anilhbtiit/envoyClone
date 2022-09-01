#include "test/extensions/http/header_validators/envoy_default/header_validator_test.h"

#include "source/extensions/http/header_validators/envoy_default/character_tables.h"
#include "source/extensions/http/header_validators/envoy_default/header_validator.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

using ::Envoy::Http::HeaderString;
using ::Envoy::Http::Protocol;
using ::Envoy::Http::RequestHeaderMap;
using ::Envoy::Http::ResponseHeaderMap;

class BaseHttpHeaderValidator : public HeaderValidator {
public:
  BaseHttpHeaderValidator(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      Protocol protocol, StreamInfo::StreamInfo& stream_info)
      : HeaderValidator(config, protocol, stream_info) {}

  HeaderEntryValidationResult validateRequestHeaderEntry(const HeaderString&,
                                                         const HeaderString&) override {
    return HeaderEntryValidationResult::success();
  }

  HeaderEntryValidationResult validateResponseHeaderEntry(const HeaderString&,
                                                          const HeaderString&) override {
    return HeaderEntryValidationResult::success();
  }

  RequestHeaderMapValidationResult validateRequestHeaderMap(RequestHeaderMap&) override {
    return RequestHeaderMapValidationResult::success();
  }

  ResponseHeaderMapValidationResult validateResponseHeaderMap(ResponseHeaderMap&) override {
    return ResponseHeaderMapValidationResult::success();
  }
};

using BaseHttpHeaderValidatorPtr = std::unique_ptr<BaseHttpHeaderValidator>;

class BaseHeaderValidatorTest : public HeaderValidatorTest {
protected:
  BaseHttpHeaderValidatorPtr createBase(absl::string_view config_yaml) {
    envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);

    return std::make_unique<BaseHttpHeaderValidator>(typed_config, Protocol::Http11, stream_info_);
  }
};

TEST_F(BaseHeaderValidatorTest, ValidateMethodPermissive) {
  HeaderString valid{"GET"};
  HeaderString valid_lowercase{"post"};
  HeaderString custom{"Custom-Method"};
  auto uhv = createBase(empty_config);
  EXPECT_TRUE(uhv->validateMethodHeader(valid).ok());
  EXPECT_TRUE(uhv->validateMethodHeader(valid_lowercase).ok());
  EXPECT_TRUE(uhv->validateMethodHeader(custom).ok());
}

TEST_F(BaseHeaderValidatorTest, ValidateMethodRestricted) {
  HeaderString valid{"GET"};
  HeaderString post_lowercase{"post"};
  HeaderString custom{"CUSTOM-METHOD"};
  auto uhv = createBase(restrict_http_methods_config);
  EXPECT_TRUE(uhv->validateMethodHeader(valid).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateMethodHeader(custom),
                             UhvResponseCodeDetail::get().InvalidMethod);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateMethodHeader(post_lowercase),
                             UhvResponseCodeDetail::get().InvalidMethod);
}

TEST_F(BaseHeaderValidatorTest, ValidateSchemeValid) {
  HeaderString valid{"https"};
  HeaderString valid_mixed_case{"hTtPs"};
  auto uhv = createBase(empty_config);

  EXPECT_TRUE(uhv->validateSchemeHeader(valid).ok());
  EXPECT_TRUE(uhv->validateSchemeHeader(valid_mixed_case).ok());
}

TEST_F(BaseHeaderValidatorTest, ValidateSchemeInvalidChar) {
  HeaderString invalid{"http_ssh"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateSchemeHeader(invalid),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(BaseHeaderValidatorTest, ValidateSchemeInvalidStartChar) {
  HeaderString invalid_first_char{"+http"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateSchemeHeader(invalid_first_char),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(BaseHeaderValidatorTest, ValidateResponseStatusRange) {
  HeaderString valid{"200"};
  HeaderString invalid_max{"1024"};
  HeaderString invalid_min{"99"};
  HeaderString invalid_overflow{"4294967297"}; // UINT32_MAX + 1
  auto uhv = createBase(empty_config);

  EXPECT_TRUE(uhv->validateStatusHeader(valid).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateStatusHeader(invalid_max),
                             UhvResponseCodeDetail::get().InvalidStatus);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateStatusHeader(invalid_min),
                             UhvResponseCodeDetail::get().InvalidStatus);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateStatusHeader(invalid_overflow),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(BaseHeaderValidatorTest, ValidateGenericHeaderName) {
  auto uhv = createBase(empty_config);
  std::string name{"aaaaa"};
  for (int i = 0; i < 0xff; ++i) {
    char c = static_cast<char>(i);
    HeaderString header_string{"x"};
    name[2] = c;

    setHeaderStringUnvalidated(header_string, name);

    auto result = uhv->validateGenericHeaderName(header_string);
    if (testChar(kGenericHeaderNameCharTable, c)) {
      EXPECT_TRUE(result.ok());
    } else {
      EXPECT_REJECT_WITH_DETAILS(result, UhvResponseCodeDetail::get().InvalidCharacters);
    }
  }
}

TEST_F(BaseHeaderValidatorTest, ValidateGenericHeaderKeyRejectUnderscores) {
  HeaderString invalid_underscore{"x_foo"};
  auto uhv = createBase(reject_headers_with_underscores_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateGenericHeaderName(invalid_underscore),
                             UhvResponseCodeDetail::get().InvalidUnderscore);
}

TEST_F(BaseHeaderValidatorTest, ValidateGenericHeaderKeyInvalidEmpty) {
  HeaderString invalid_empty{""};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateGenericHeaderName(invalid_empty),
                             UhvResponseCodeDetail::get().EmptyHeaderName);
}

TEST_F(BaseHeaderValidatorTest, ValidateGenericHeaderKeyDropUnderscores) {
  HeaderString drop_underscore{"x_foo"};
  auto uhv = createBase(drop_headers_with_underscores_config);

  auto result = uhv->validateGenericHeaderName(drop_underscore);
  EXPECT_EQ(result.action(), decltype(result)::Action::DropHeader);
  EXPECT_EQ(result.details(), UhvResponseCodeDetail::get().InvalidUnderscore);
}

TEST_F(BaseHeaderValidatorTest, ValidateGenericHeaderValue) {
  auto uhv = createBase(empty_config);
  std::string name{"aaaaa"};
  for (int i = 0; i < 0xff; ++i) {
    char c = static_cast<char>(i);
    HeaderString header_string{"x"};
    name[2] = c;

    setHeaderStringUnvalidated(header_string, name);

    auto result = uhv->validateGenericHeaderValue(header_string);
    if (testChar(kGenericHeaderValueCharTable, c)) {
      EXPECT_TRUE(result.ok());
    } else {
      EXPECT_REJECT_WITH_DETAILS(result, UhvResponseCodeDetail::get().InvalidCharacters);
    }
  }
}

TEST_F(BaseHeaderValidatorTest, ValidateContentLength) {
  HeaderString valid{"100"};
  HeaderString invalid{"10a2"};
  HeaderString invalid_overflow{"18446744073709551618"}; // UINT64_MAX + 1
  auto uhv = createBase(empty_config);

  EXPECT_TRUE(uhv->validateContentLengthHeader(valid).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateContentLengthHeader(invalid),
                             UhvResponseCodeDetail::get().InvalidContentLength);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateContentLengthHeader(invalid_overflow),
                             UhvResponseCodeDetail::get().InvalidContentLength);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderValidRegName) {
  HeaderString valid{"envoy.com:443"};
  HeaderString valid_no_port{"envoy.com"};
  auto uhv = createBase(empty_config);

  EXPECT_TRUE(uhv->validateHostHeader(valid).ok());
  EXPECT_TRUE(uhv->validateHostHeader(valid_no_port).ok());
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidRegName) {
  HeaderString invalid{"env<o>y.com"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderValidIPv6) {
  HeaderString valid{"[2001:0db8:85a3:0000:0000:8a2e:0370:7334]:443"};
  HeaderString valid_no_port{"[2001:0db8:85a3:0000:0000:8a2e:0370:7334]"};
  auto uhv = createBase(empty_config);

  EXPECT_TRUE(uhv->validateHostHeader(valid).ok());
  EXPECT_TRUE(uhv->validateHostHeader(valid_no_port).ok());
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidIPv6) {
  HeaderString invalid_missing_closing_bracket{"[2001:0db8:85a3:0000:0000:8a2e:0370:7334"};
  HeaderString invalid_chars{"[200z:0db8:85a3:0000:0000:8a2e:0370:7334]"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_missing_closing_bracket),
                             UhvResponseCodeDetail::get().InvalidHost);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_chars),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidEmpty) {
  HeaderString invalid_empty{""};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_empty),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidUserInfo) {
  HeaderString invalid_userinfo{"foo:bar@envoy.com"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_userinfo),
                             UhvResponseCodeDetail::get().InvalidHostDeprecatedUserInfo);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidPortNumber) {
  HeaderString invalid_port_int{"envoy.com:a"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_port_int),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidPortTrailer) {
  HeaderString invalid_port_trailer{"envoy.com:10a"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_port_trailer),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidPortMax) {
  HeaderString invalid_port_value{"envoy.com:66000"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_port_value),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidPort0) {
  HeaderString invalid_port_0{"envoy.com:0"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_port_0),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidIPv6PortDelim) {
  HeaderString invalid_port_delim{"[2001:0db8:85a3:0000:0000:8a2e:0370:7334]66000"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_port_delim),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(BaseHeaderValidatorTest, ValidatePathHeaderCharacters) {
  HeaderString valid{"/"};
  auto uhv = createBase(empty_config);

  EXPECT_TRUE(uhv->validatePathHeaderCharacters(valid).ok());
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
