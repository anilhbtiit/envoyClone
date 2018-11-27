#include "common/common/stack_array.h"

#include "extensions/filters/network/kafka/kafka_request_parser.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

class BufferBasedTest : public testing::Test {
public:
  Buffer::OwnedImpl& buffer() { return buffer_; }

  const char* getBytes() {
    uint64_t num_slices = buffer_.getRawSlices(nullptr, 0);
    STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
    buffer_.getRawSlices(slices.begin(), num_slices);
    return reinterpret_cast<const char*>((slices[0]).mem_);
  }

protected:
  Buffer::OwnedImpl buffer_;
  EncodingContext encoder_{-1}; // api_version is not used for request header
};

class MockRequestParserResolver : public RequestParserResolver {
public:
  MockRequestParserResolver(){};
  MOCK_CONST_METHOD3(createParser, ParserSharedPtr(int16_t, int16_t, RequestContextSharedPtr));
};

TEST_F(BufferBasedTest, RequestStartParserTestShouldReturnRequestHeaderParser) {
  // given
  MockRequestParserResolver resolver{};
  RequestStartParser testee{resolver};

  int32_t request_len = 1234;
  encoder_.encode(request_len, buffer());

  const char* bytes = getBytes();
  uint64_t remaining = 1024;

  // when
  const ParseResponse result = testee.parse(bytes, remaining);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_NE(std::dynamic_pointer_cast<RequestHeaderParser>(result.next_parser_), nullptr);
  ASSERT_EQ(result.message_, nullptr);
  ASSERT_EQ(testee.contextForTest()->remaining_request_size_, request_len);
}

class MockParser : public Parser {
public:
  ParseResponse parse(const char*&, uint64_t&) {
    throw new EnvoyException("should not be invoked");
  }
};

TEST_F(BufferBasedTest, RequestHeaderParserShouldExtractHeaderDataAndResolveNextParser) {
  // given
  const MockRequestParserResolver parser_resolver;
  const ParserSharedPtr parser{new MockParser{}};
  EXPECT_CALL(parser_resolver, createParser(_, _, _)).WillOnce(Return(parser));

  const int32_t request_len = 1000;
  RequestContextSharedPtr context{new RequestContext()};
  context->remaining_request_size_ = request_len;
  RequestHeaderParser testee{parser_resolver, context};

  const int16_t api_key{1};
  const int16_t api_version{2};
  const int32_t correlation_id{10};
  const NullableString client_id{"aaa"};
  size_t written = 0;
  written += encoder_.encode(api_key, buffer());
  written += encoder_.encode(api_version, buffer());
  written += encoder_.encode(correlation_id, buffer());
  written += encoder_.encode(client_id, buffer());

  const char* bytes = getBytes();
  uint64_t remaining = 100000;
  const uint64_t orig_remaining = remaining;

  // when
  const ParseResponse result = testee.parse(bytes, remaining);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_EQ(result.next_parser_, parser);
  ASSERT_EQ(result.message_, nullptr);

  ASSERT_EQ(testee.contextForTest()->remaining_request_size_, request_len - written);
  ASSERT_EQ(remaining, orig_remaining - written);

  const RequestHeader expected_header{api_key, api_version, correlation_id, client_id};
  ASSERT_EQ(testee.contextForTest()->request_header_, expected_header);
}

TEST_F(BufferBasedTest, RequestHeaderParserShouldHandleDeserializerExceptionsDuringFeeding) {
  // given

  // throws during feeding
  class ThrowingRequestHeaderDeserializer : public RequestHeaderDeserializer {
  public:
    size_t feed(const char*&, uint64_t&) { throw EnvoyException("feed"); };

    bool ready() const { throw std::runtime_error("should not be invoked at all"); };

    RequestHeader get() const { throw std::runtime_error("should not be invoked at all"); };
  };

  const MockRequestParserResolver parser_resolver;

  const int32_t request_size = 1024; // there are still 1024 bytes to read to complete the request
  RequestContextSharedPtr request_context{new RequestContext{request_size, {}}};
  RequestHeaderParser testee{parser_resolver, request_context,
                             std::make_unique<ThrowingRequestHeaderDeserializer>()};

  const char* bytes = getBytes();
  const char* orig_bytes = bytes;
  uint64_t remaining = 100000;
  const uint64_t orig_remaining = remaining;

  // when
  const ParseResponse result = testee.parse(bytes, remaining);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_EQ(result.next_parser_, nullptr);
  ASSERT_NE(std::dynamic_pointer_cast<UnknownRequest>(result.message_), nullptr);

  ASSERT_EQ(bytes, orig_bytes + request_size);
  ASSERT_EQ(remaining, orig_remaining - request_size);

  ASSERT_EQ(testee.contextForTest()->remaining_request_size_, 0);
}

TEST_F(BufferBasedTest, RequestParserShouldHandleDeserializerExceptionsDuringFeeding) {
  // given

  // throws during feeding
  class ThrowingDeserializer : public Deserializer<int32_t> {
  public:
    size_t feed(const char*&, uint64_t&) { throw EnvoyException("feed"); };

    bool ready() const { throw std::runtime_error("should not be invoked at all"); };

    int32_t get() const { throw std::runtime_error("should not be invoked at all"); };
  };

  const int32_t request_size = 1024; // there are still 1024 bytes to read to complete the request
  RequestContextSharedPtr request_context{new RequestContext{request_size, {}}};

  RequestParser<int32_t, ThrowingDeserializer> testee{request_context};

  const char* bytes = getBytes();
  const char* orig_bytes = bytes;
  uint64_t remaining = 100000;
  const uint64_t orig_remaining = remaining;

  // when
  const ParseResponse result = testee.parse(bytes, remaining);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_EQ(result.next_parser_, nullptr);
  ASSERT_NE(std::dynamic_pointer_cast<UnknownRequest>(result.message_), nullptr);

  ASSERT_EQ(bytes, orig_bytes + request_size);
  ASSERT_EQ(remaining, orig_remaining - request_size);
}

// deserializer that consumes 4 bytes and returns 0
class FourBytesDeserializer : public Deserializer<int32_t> {
public:
  size_t feed(const char*& buffer, uint64_t& remaining) {
    buffer += 4;
    remaining -= 4;
    return 4;
  };

  bool ready() const { return true; };

  int32_t get() const { return 0; };
};

TEST_F(BufferBasedTest, RequestParserShouldHandleDeserializerClaimingItsReadyButLeavingData) {
  // given
  const int32_t request_size = 1024; // there are still 1024 bytes to read to complete the request
  RequestContextSharedPtr request_context{new RequestContext{request_size, {}}};

  RequestParser<int32_t, FourBytesDeserializer> testee{request_context};

  const char* bytes = getBytes();
  const char* orig_bytes = bytes;
  uint64_t remaining = 100000;
  const uint64_t orig_remaining = remaining;

  // when
  const ParseResponse result = testee.parse(bytes, remaining);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_EQ(result.next_parser_, nullptr);
  ASSERT_NE(std::dynamic_pointer_cast<UnknownRequest>(result.message_), nullptr);

  ASSERT_EQ(bytes, orig_bytes + request_size);
  ASSERT_EQ(remaining, orig_remaining - request_size);
}

TEST_F(BufferBasedTest, SentinelParserShouldConsumeDataUntilEndOfRequest) {
  // given
  const int32_t request_len = 1000;
  RequestContextSharedPtr context{new RequestContext()};
  context->remaining_request_size_ = request_len;
  SentinelParser testee{context};

  const Bytes garbage(request_len * 2);
  encoder_.encode(garbage, buffer());

  const char* bytes = getBytes();
  uint64_t remaining = request_len * 2;
  const uint64_t orig_remaining = remaining;

  // when
  const ParseResponse result = testee.parse(bytes, remaining);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_EQ(result.next_parser_, nullptr);
  ASSERT_NE(std::dynamic_pointer_cast<UnknownRequest>(result.message_), nullptr);

  ASSERT_EQ(testee.contextForTest()->remaining_request_size_, 0);
  ASSERT_EQ(remaining, orig_remaining - request_len);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
