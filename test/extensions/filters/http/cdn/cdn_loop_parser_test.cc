#include "extensions/filters/http/cdn/cdn_loop_parser.h"

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/extensions/filters/http/cdn/status_helpers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cdn {
namespace CdnLoopParser {
namespace {

using StatusHelpers::IsOkAndHolds;
using StatusHelpers::StatusIs;

TEST(ParseOptionalWhitespaceTest, TestEmpty) {
  const std::string value = "";
  ParseContext input(value);
  EXPECT_EQ(ParseOptionalWhitespace(input), (ParseContext(value, 0)));
}

TEST(ParseOptionalWhitespaceTest, TestSpace) {
  const std::string value = " ";
  ParseContext input(value);
  EXPECT_EQ(ParseOptionalWhitespace(input), (ParseContext(value, 1)));
}

TEST(ParseOptionalWhitespaceTest, TestTab) {
  const std::string value = "\t";
  ParseContext input(value);
  EXPECT_EQ(ParseOptionalWhitespace(input), (ParseContext(value, 1)));
}

TEST(ParseOptionalWhitespaceTest, TestLots) {
  const std::string value = "   \t \t ";
  ParseContext input(value);
  EXPECT_EQ(ParseOptionalWhitespace(input), (ParseContext(value, 7)));
}

TEST(ParseOptionalWhitespaceTest, NoWhitespace) {
  const std::string value = "c";
  ParseContext input(value);
  EXPECT_EQ(ParseOptionalWhitespace(input), (ParseContext(value, 0)));
}

TEST(ParseOptionalWhitespaceTest, StopsAtNonWhitespace) {
  const std::string value = "  c";
  ParseContext input(value);
  EXPECT_EQ(ParseOptionalWhitespace(input), (ParseContext(value, 2)));
}

TEST(ParseQuotedPairTest, Simple) {
  const std::string value = R"(\a)";
  ParseContext input(value);
  EXPECT_THAT(ParseQuotedPair(input), IsOkAndHolds(ParseContext(value, 2)));
}

TEST(ParseQuotedPairTest, EndOfInput) {
  const std::string value = R"(a)";
  ParseContext input(value);
  EXPECT_THAT(ParseQuotedPair(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseQuotedPairTest, MissingQuotable) {
  const std::string value = R"(\)";
  ParseContext input(value);
  EXPECT_THAT(ParseQuotedPair(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseQuotedPairTest, BadQuotable) {
  const std::string value = "\\\x1f";
  ParseContext input(value);
  EXPECT_THAT(ParseQuotedPair(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseQuotedPairTest, MissingBackslash) {
  const std::string value = R"(a)";
  ParseContext input(value);
  EXPECT_THAT(ParseQuotedPair(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseQuotedStringTest, Simple) {
  const std::string value = "\"abcd\"";
  ParseContext input(value);
  EXPECT_THAT(ParseQuotedString(input), IsOkAndHolds(ParseContext(value, 6)));
}

TEST(ParseQuotedStringTest, QdStringEdgeCases) {
  const std::string value = "\"\t \x21\x23\x5b\x5d\x7e\x80\xff\"";
  ParseContext input(value);
  EXPECT_THAT(ParseQuotedString(input), IsOkAndHolds(ParseContext(value, 11)));
}

TEST(ParseQuotedStringTest, QuotedPair) {
  const std::string value = "\"\\\"\"";
  ParseContext input(value);
  EXPECT_THAT(ParseQuotedString(input), IsOkAndHolds(ParseContext(value, 4)));
}

TEST(ParseQuotedStringTest, NoStartQuote) {
  const std::string value = "foo";
  ParseContext input(value);
  EXPECT_THAT(ParseQuotedString(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseQuotedStringTest, EndOfInput) {
  const std::string value = "\"missing-final-dquote";
  ParseContext input(value);
  EXPECT_THAT(ParseQuotedString(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseQuotedStringTest, NonVisualChar) {
  const std::string value = "\"\x1f\"";
  ParseContext input(value);
  EXPECT_THAT(ParseQuotedString(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseQuotedStringTest, QuotedPairEdgeCases) {
  const std::string value = "\"\\";
  ParseContext input(value);
  EXPECT_THAT(ParseQuotedString(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseTokenTest, AllValues) {
  const std::string value = "!#$%&'*+-.^_`|~09azAZ";
  ParseContext input(value);
  EXPECT_THAT(ParseToken(input), IsOkAndHolds(ParseContext(value, 21)));
}

TEST(ParseTokenTest, TwoTokens) {
  const std::string value = "token1 token2";
  {
    ParseContext input(value);
    EXPECT_THAT(ParseToken(input), IsOkAndHolds(ParseContext(value, 6)));
  }
  {
    ParseContext input{.value = value, .next = 6};
    EXPECT_THAT(ParseToken(input), StatusIs(absl::StatusCode::kInvalidArgument));
  }
  {
    ParseContext input{.value = value, .next = 7};
    EXPECT_THAT(ParseToken(input), IsOkAndHolds(ParseContext(value, 13)));
  }
}

TEST(ParsePlausibleIpV6, Example) {
  const std::string value = "[2001:DB8::1]";
  ParseContext input(value);
  EXPECT_THAT(ParsePlausibleIpV6(input), IsOkAndHolds(ParseContext(value, 13)));
}

TEST(ParsePlausibleIpV6, ExampleLowerCase) {
  const std::string value = "[2001:db8::1]";
  ParseContext input(value);
  EXPECT_THAT(ParsePlausibleIpV6(input), IsOkAndHolds(ParseContext(value, 13)));
}

TEST(ParsePlausibleIpV6, ExampleIpV4) {
  const std::string value = "[2001:db8::192.0.2.0]";
  ParseContext input(value);
  EXPECT_THAT(ParsePlausibleIpV6(input), IsOkAndHolds(ParseContext(value, 21)));
}

TEST(ParsePlausibleIpV6, AllHexValues) {
  const std::string value = "[1234:5678:90aA:bBcC:dDeE:fF00]";
  ParseContext input(value);
  EXPECT_THAT(ParsePlausibleIpV6(input), IsOkAndHolds(ParseContext(value, 31)));
}

TEST(ParsePlausibleIpV6, EmptyInput) {
  const std::string value = "";
  ParseContext input(value);
  EXPECT_THAT(ParsePlausibleIpV6(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParsePlausibleIpV6, BadStartDelimiter) {
  const std::string value = "{2001:DB8::1}";
  ParseContext input(value);
  EXPECT_THAT(ParsePlausibleIpV6(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParsePlausibleIpV6, BadCharacter) {
  const std::string value = "[hello]";
  ParseContext input(value);
  EXPECT_THAT(ParsePlausibleIpV6(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParsePlausibleIpV6, BadEndDelimiter) {
  const std::string value = "[2001:DB8::1}";
  ParseContext input(value);
  EXPECT_THAT(ParsePlausibleIpV6(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParsePlausibleIpV6, EndBeforeDelimiter) {
  const std::string value = "[2001:DB8::1";
  ParseContext input(value);
  EXPECT_THAT(ParsePlausibleIpV6(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseCdnIdTest, Simple) {
  const std::string value = "name";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnId(input), IsOkAndHolds(ParsedCdnId(ParseContext(value, 4), "name")));
}

TEST(ParseCdnIdTest, SecondInSeries) {
  // This showcases a bug where I thought absl::string_view::substr took (start,
  // end) arguments, not (start, len).
  const std::string value = "cdn1, cdn2, cdn3";
  ParseContext input{.value = value, .next = 6};
  EXPECT_THAT(ParseCdnId(input), IsOkAndHolds(ParsedCdnId(ParseContext(value, 10), "cdn2")));
}

TEST(ParseCdnIdTest, Empty) {
  const std::string value = "";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnId(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseCdnIdTest, NotValidTokenOrUri) {
  const std::string value = ",";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnId(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseCdnIdTest, UriHostName) {
  const std::string value = "www.example.com";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnId(input),
              IsOkAndHolds(ParsedCdnId(ParseContext(value, 15), "www.example.com")));
}

TEST(ParseCdnIdTest, UriHostNamePort) {
  const std::string value = "www.example.com:443";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnId(input),
              IsOkAndHolds(ParsedCdnId(ParseContext(value, 19), "www.example.com:443")));
}

TEST(ParseCdnIdTest, UriHostNameBlankPort) {
  const std::string value = "www.example.com:";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnId(input),
              IsOkAndHolds(ParsedCdnId(ParseContext(value, 16), "www.example.com:")));
}

TEST(ParseCdnIdTest, UriHostIpV4) {
  const std::string value = "192.0.2.0";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnId(input), IsOkAndHolds(ParsedCdnId(ParseContext(value, 9), "192.0.2.0")));
}

TEST(ParseCdnIdTest, UriHostIpV4Port) {
  const std::string value = "192.0.2.0:443";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnId(input),
              IsOkAndHolds(ParsedCdnId(ParseContext(value, 13), "192.0.2.0:443")));
}

TEST(ParseCdnIdTest, UriHostIpV4BlankPort) {
  const std::string value = "192.0.2.0:";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnId(input), IsOkAndHolds(ParsedCdnId(ParseContext(value, 10), "192.0.2.0:")));
}

TEST(ParseCdnIdTest, UriHostIpV6) {
  const std::string value = "[2001:DB8::1]";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnId(input),
              IsOkAndHolds(ParsedCdnId(ParseContext(value, 13), "[2001:DB8::1]")));
}

TEST(ParseCdnIdTest, UriHostIpV6Port) {
  const std::string value = "[2001:DB8::1]:443";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnId(input),
              IsOkAndHolds(ParsedCdnId(ParseContext(value, 17), "[2001:DB8::1]:443")));
}

TEST(ParseCdnIdTest, UriHostIpV6BlankPort) {
  const std::string value = "[2001:DB8::1]:";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnId(input),
              IsOkAndHolds(ParsedCdnId(ParseContext(value, 14), "[2001:DB8::1]:")));
}

TEST(ParseParameterTest, SimpleTokenValue) {
  const std::string value = "a=b";
  ParseContext input(value);
  EXPECT_THAT(ParseParameter(input), IsOkAndHolds(ParseContext(value, 3)));
}

TEST(ParseParameterTest, SimpleQuotedValue) {
  const std::string value = "a=\"b\"";
  ParseContext input(value);
  EXPECT_THAT(ParseParameter(input), IsOkAndHolds(ParseContext(value, 5)));
}

TEST(ParseParameterTest, TruncatedBeforeEquals) {
  const std::string value = "a";
  ParseContext input(value);
  EXPECT_THAT(ParseParameter(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseParameterTest, MissingEquals) {
  const std::string value = "a,";
  ParseContext input(value);
  EXPECT_THAT(ParseParameter(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseParameterTest, ValueNotToken) {
  const std::string value = "a=,";
  ParseContext input(value);
  EXPECT_THAT(ParseParameter(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseParameterTest, ValueNotQuotedString) {
  const std::string value = "a=\"";
  ParseContext input(value);
  EXPECT_THAT(ParseParameter(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseCdnInfoTest, Simple) {
  const std::string value = "name";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnInfo(input), IsOkAndHolds(ParsedCdnInfo(ParseContext(value, 4), "name")));
}

TEST(ParseCdnInfoTest, Empty) {
  const std::string value = "";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnInfo(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseCdnInfoTest, NotValidTokenOrUri) {
  const std::string value = ",";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnInfo(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseCdnInfoTest, SingleParameter) {
  const std::string value = "name;a=b";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnInfo(input), IsOkAndHolds(ParsedCdnInfo(ParseContext(value, 8), "name")));
}

TEST(ParseCdnInfoTest, SingleParameterExtraWhitespace) {
  const std::string value = "name ; a=b  ";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnInfo(input), IsOkAndHolds(ParsedCdnInfo(ParseContext(value, 12), "name")));
}

TEST(ParseCdnInfoTest, MultipleParametersWithWhitespace) {
  const std::string value = "name ; a=b ; c=\"d\" ; e=\";\" ";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnInfo(input), IsOkAndHolds(ParsedCdnInfo(ParseContext(value, 27), "name")));
}

TEST(ParseCdnInfoTest, MissingParameter) {
  const std::string value = "name ; ";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnInfo(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseCdnInfoTest, InvalidParameter) {
  const std::string value = "name ; a= ";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnInfo(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseCdnInfoListTest, Simple) {
  const std::string value = "cdn1, cdn2, cdn3";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnInfoList(input),
              IsOkAndHolds(ParsedCdnInfoList(ParseContext(value, 16), {"cdn1", "cdn2", "cdn3"})));
}

TEST(ParseCdnInfoListTest, ExtraWhitespace) {
  const std::string value = " \t cdn1 \t , cdn2  \t  ,  \t cdn3   ";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnInfoList(input),
              IsOkAndHolds(ParsedCdnInfoList(ParseContext(value, 33), {"cdn1", "cdn2", "cdn3"})));
}

TEST(ParseCdnInfoListTest, InvalidParseNoComma) {
  const std::string value = "cdn1 cdn2";
  ParseContext input(value);
  EXPECT_THAT(ParseCdnInfoList(input), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ParseCdnInfoListTest, Rfc7230Section7Tests) {
  // These are the examples from https://tools.ietf.org/html/rfc7230#section-7
  {
    const std::string value = "foo,bar";
    ParseContext input(value);
    EXPECT_THAT(ParseCdnInfoList(input),
                IsOkAndHolds(ParsedCdnInfoList(ParseContext(value, 7), {"foo", "bar"})));
  }
  {
    const std::string value = "foo ,bar,";
    ParseContext input(value);
    EXPECT_THAT(ParseCdnInfoList(input),
                IsOkAndHolds(ParsedCdnInfoList(ParseContext(value, 9), {"foo", "bar"})));
  }
  {
    const std::string value = "foo , ,bar,charlie   ";
    ParseContext input(value);
    EXPECT_THAT(ParseCdnInfoList(input), IsOkAndHolds(ParsedCdnInfoList(
                                             ParseContext(value, 21), {"foo", "bar", "charlie"})));
  }
  // The following tests are allowed in the #cdn-info rule because it doesn't
  // require a single element.
  {
    const std::string value = "";
    ParseContext input(value);

    EXPECT_THAT(ParseCdnInfoList(input),
                IsOkAndHolds(ParsedCdnInfoList(ParseContext(value, 0), {})));
  }
  {
    const std::string value = ",";
    ParseContext input(value);
    EXPECT_THAT(ParseCdnInfoList(input),
                IsOkAndHolds(ParsedCdnInfoList(ParseContext(value, 1), {})));
  }
  {
    const std::string value = ",   ,";
    ParseContext input(value);
    EXPECT_THAT(ParseCdnInfoList(input),
                IsOkAndHolds(ParsedCdnInfoList(ParseContext(value, 5), {})));
  }
}

} // namespace
} // namespace CdnLoopParser
} // namespace Cdn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
