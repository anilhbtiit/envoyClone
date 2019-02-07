#include "envoy/http/header_map.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
using ::testing::_;
using ::testing::Not;

namespace Http {
using HeaderValueOfTest = TestBase;

TEST_F(HeaderValueOfTest, ConstHeaderMap) {
  const TestHeaderMapImpl header_map{{"key", "expected value"}};

  // Positive checks.
  EXPECT_THAT(header_map, HeaderValueOf("key", "expected value"));
  EXPECT_THAT(header_map, HeaderValueOf("key", _));

  // Negative checks.
  EXPECT_THAT(header_map, Not(HeaderValueOf("key", "other value")));
  EXPECT_THAT(header_map, Not(HeaderValueOf("other key", _)));
}

TEST_F(HeaderValueOfTest, MutableHeaderMap) {
  TestHeaderMapImpl header_map;

  // Negative checks.
  EXPECT_THAT(header_map, Not(HeaderValueOf("key", "other value")));
  EXPECT_THAT(header_map, Not(HeaderValueOf("other key", _)));

  header_map.addCopy("key", "expected value");

  // Positive checks.
  EXPECT_THAT(header_map, HeaderValueOf("key", "expected value"));
  EXPECT_THAT(header_map, HeaderValueOf("key", _));
}

TEST_F(HeaderValueOfTest, LowerCaseString) {
  TestHeaderMapImpl header_map;
  LowerCaseString key("key");
  LowerCaseString other_key("other_key");

  // Negative checks.
  EXPECT_THAT(header_map, Not(HeaderValueOf(key, "other value")));
  EXPECT_THAT(header_map, Not(HeaderValueOf(other_key, _)));

  header_map.addCopy(key, "expected value");
  header_map.addCopy(other_key, "ValUe");

  // Positive checks.
  EXPECT_THAT(header_map, HeaderValueOf(key, "expected value"));
  EXPECT_THAT(header_map, HeaderValueOf(other_key, _));
}

using HttpStatusIsTest = TestBase;

TEST_F(HttpStatusIsTest, CheckStatus) {
  TestHeaderMapImpl header_map;
  const auto status_matcher = HttpStatusIs(200);

  EXPECT_THAT(header_map, Not(status_matcher));

  header_map.addCopy(Headers::get().Status, "200");

  EXPECT_THAT(header_map, status_matcher);
}

using IsSubsetOfHeadersTest = TestBase;

TEST_F(IsSubsetOfHeadersTest, ConstHeaderMap) {
  const TestHeaderMapImpl header_map{{"first key", "1"}};

  EXPECT_THAT(header_map, IsSubsetOfHeaders(TestHeaderMapImpl{{"first key", "1"}}));
  EXPECT_THAT(header_map,
              IsSubsetOfHeaders(TestHeaderMapImpl{{"first key", "1"}, {"second key", "2"}}));

  EXPECT_THAT(header_map, Not(IsSubsetOfHeaders(TestHeaderMapImpl{{"third key", "1"}})));
}

TEST_F(IsSubsetOfHeadersTest, MutableHeaderMap) {
  TestHeaderMapImpl header_map;
  header_map.addCopy("first key", "1");

  EXPECT_THAT(header_map, IsSubsetOfHeaders(TestHeaderMapImpl{{"first key", "1"}}));
  EXPECT_THAT(header_map,
              IsSubsetOfHeaders(TestHeaderMapImpl{{"first key", "1"}, {"second key", "2"}}));

  EXPECT_THAT(header_map, Not(IsSubsetOfHeaders(TestHeaderMapImpl{{"third key", "1"}})));
}

using IsSupersetOfHeadersTest = TestBase;

TEST_F(IsSupersetOfHeadersTest, ConstHeaderMap) {
  const TestHeaderMapImpl header_map{{"first key", "1"}, {"second key", "2"}};

  EXPECT_THAT(header_map,
              IsSupersetOfHeaders(TestHeaderMapImpl{{"first key", "1"}, {"second key", "2"}}));
  EXPECT_THAT(header_map, IsSupersetOfHeaders(TestHeaderMapImpl{{"first key", "1"}}));

  EXPECT_THAT(header_map, Not(IsSupersetOfHeaders(TestHeaderMapImpl{{"third key", "1"}})));
}

TEST_F(IsSupersetOfHeadersTest, MutableHeaderMap) {
  TestHeaderMapImpl header_map;
  header_map.addCopy("first key", "1");
  header_map.addCopy("second key", "2");

  EXPECT_THAT(header_map,
              IsSupersetOfHeaders(TestHeaderMapImpl{{"first key", "1"}, {"second key", "2"}}));
  EXPECT_THAT(header_map, IsSupersetOfHeaders(TestHeaderMapImpl{{"first key", "1"}}));

  EXPECT_THAT(header_map, Not(IsSupersetOfHeaders(TestHeaderMapImpl{{"third key", "1"}})));
}
} // namespace Http

using HeaderHasValueRefTest = TestBase;

TEST_F(HeaderHasValueRefTest, MutableValueRef) {
  Http::TestHeaderMapImpl header_map;

  EXPECT_THAT(header_map, Not(HeaderHasValueRef("key", "value")));
  EXPECT_THAT(header_map, Not(HeaderHasValueRef("other key", "value")));

  header_map.addCopy("key", "value");

  EXPECT_THAT(header_map, HeaderHasValueRef("key", "value"));
  EXPECT_THAT(header_map, Not(HeaderHasValueRef("key", "wrong value")));
}

TEST_F(HeaderHasValueRefTest, ConstValueRef) {
  const Http::TestHeaderMapImpl header_map{{"key", "expected value"}};

  EXPECT_THAT(header_map, Not(HeaderHasValueRef("key", "other value")));
  EXPECT_THAT(header_map, HeaderHasValueRef("key", "expected value"));
}

TEST_F(HeaderHasValueRefTest, LowerCaseStringArguments) {
  Http::LowerCaseString key("key"), other_key("other key");
  Http::TestHeaderMapImpl header_map;

  EXPECT_THAT(header_map, Not(HeaderHasValueRef(key, "value")));
  EXPECT_THAT(header_map, Not(HeaderHasValueRef(other_key, "value")));

  header_map.addCopy(key, "value");

  EXPECT_THAT(header_map, HeaderHasValueRef(key, "value"));
  EXPECT_THAT(header_map, Not(HeaderHasValueRef(other_key, "wrong value")));
}
} // namespace Envoy
