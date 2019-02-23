#pragma once

#include <memory>

#include "envoy/common/exception.h"

#include "common/common/assert.h"

#include "extensions/filters/network/kafka/kafka_request.h"
#include "extensions/filters/network/kafka/parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Context that is shared between parsers that are handling the same single message
 */
struct RequestContext {
  int32_t remaining_request_size_{0};
  RequestHeader request_header_{};
};

typedef std::shared_ptr<RequestContext> RequestContextSharedPtr;

/**
 * Configuration object
 * Resolves the parser that will be responsible for consuming the request-specific data
 * In other words: provides (api_key, api_version) -> Parser function
 */
class RequestParserResolver {
public:
  virtual ~RequestParserResolver() = default;

  /**
   * Creates a parser that is going to process data specific for given api_key & api_version
   * @param api_key request type
   * @param api_version request version
   * @param context context to be used by parser
   * @return parser that is capable of processing data for given request type & version
   */
  virtual ParserSharedPtr createParser(int16_t api_key, int16_t api_version,
                                       RequestContextSharedPtr context) const;

  /**
   * Request parser singleton
   */
  static const RequestParserResolver INSTANCE;
};

/**
 * Request parser responsible for consuming request length and setting up context with this data
 * @see http://kafka.apache.org/protocol.html#protocol_common
 */
class RequestStartParser : public Parser {
public:
  RequestStartParser(const RequestParserResolver& parser_resolver)
      : parser_resolver_{parser_resolver}, context_{std::make_shared<RequestContext>()} {};

  /**
   * Consumes INT32 bytes as request length and updates the context with that value
   * @return RequestHeaderParser instance to process request header
   */
  ParseResponse parse(const char*& buffer, uint64_t& remaining) override;

  const RequestContextSharedPtr contextForTest() const { return context_; }

private:
  const RequestParserResolver& parser_resolver_;
  const RequestContextSharedPtr context_;
  Int32Deserializer request_length_;
};

/**
 * Deserializer that extracts request header (4 fields)
 * Can throw, as one of the fields (client-id) can throw (nullable string with invalid length)
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
class RequestHeaderDeserializer
    : public CompositeDeserializerWith4Delegates<RequestHeader, Int16Deserializer,
                                                 Int16Deserializer, Int32Deserializer,
                                                 NullableStringDeserializer> {};

typedef std::unique_ptr<RequestHeaderDeserializer> RequestHeaderDeserializerPtr;

/**
 * Parser responsible for computing request header and updating the context with data resolved
 * On a successful parse uses resolved data (api_key & api_version) to determine next parser.
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
class RequestHeaderParser : public Parser {
public:
  // default constructor
  RequestHeaderParser(const RequestParserResolver& parser_resolver, RequestContextSharedPtr context)
      : RequestHeaderParser{parser_resolver, context,
                            std::make_unique<RequestHeaderDeserializer>()} {};

  // visible for testing
  RequestHeaderParser(const RequestParserResolver& parser_resolver, RequestContextSharedPtr context,
                      RequestHeaderDeserializerPtr deserializer)
      : parser_resolver_{parser_resolver}, context_{context}, deserializer_{
                                                                  std::move(deserializer)} {};

  /**
   * Uses data provided to compute request header
   * @return Parser instance responsible for processing rest of the message
   */
  ParseResponse parse(const char*& buffer, uint64_t& remaining) override;

  const RequestContextSharedPtr contextForTest() const { return context_; }

private:
  const RequestParserResolver& parser_resolver_;
  const RequestContextSharedPtr context_;
  RequestHeaderDeserializerPtr deserializer_;
};

/**
 * Sentinel parser that is responsible for consuming message bytes for messages that had unsupported
 * api_key & api_version. It does not attempt to capture any data, just throws it away until end of
 * message
 */
class SentinelParser : public Parser {
public:
  SentinelParser(RequestContextSharedPtr context) : context_{context} {};

  /**
   * Returns UnknownRequest
   */
  ParseResponse parse(const char*& buffer, uint64_t& remaining) override;

  const RequestContextSharedPtr contextForTest() const { return context_; }

private:
  const RequestContextSharedPtr context_;
};

/**
 * Request parser uses a single deserializer to construct a request object
 * This parser is responsible for consuming request-specific data (e.g. topic names) and always
 * returns a parsed message
 * @param RequestType request class
 * @param DeserializerType deserializer type corresponding to request class (should be subclass of
 * Deserializer<RequestType>)
 */
template <typename RequestType, typename DeserializerType> class RequestParser : public Parser {
public:
  /**
   * Create a parser with given context
   * @param context parse context containing request header
   */
  RequestParser(RequestContextSharedPtr context) : context_{context} {};

  /**
   * Consume enough data to fill in deserializer and receive the parsed request
   * Fill in request's header with data stored in context
   */
  ParseResponse parse(const char*& buffer, uint64_t& remaining) override {
    const uint64_t orig_remaining = remaining;
    try {
      context_->remaining_request_size_ -= deserializer.feed(buffer, remaining);
    } catch (const EnvoyException&) {
      // treat the whole request as invalid, throw away the rest of the data
      const int32_t consumed = static_cast<int32_t>(orig_remaining - remaining);
      context_->remaining_request_size_ -=
          consumed; // some of the data might have been consumed by throwing deserializer
      return ParseResponse::nextParser(std::make_shared<SentinelParser>(context_));
    }

    if (deserializer.ready()) {
      if (0 == context_->remaining_request_size_) {
        // after a successful parse, there should be nothing left - we have consumed all the bytes
        MessageSharedPtr msg = std::make_shared<ConcreteRequest<RequestType>>(
            context_->request_header_, deserializer.get());
        return ParseResponse::parsedMessage(msg);
      } else {
        // the message makes no sense, the deserializer that matches the schema consumed all
        // necessary data, but there's still unconsumed bytes
        return ParseResponse::nextParser(std::make_shared<SentinelParser>(context_));
      }
    } else {
      return ParseResponse::stillWaiting();
    }
  }

  const RequestContextSharedPtr contextForTest() const { return context_; }

protected:
  RequestContextSharedPtr context_;
  DeserializerType deserializer; // underlying request-specific deserializer
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
