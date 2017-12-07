#include "common/router/header_parser.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Router {

namespace {

HeaderFormatterPtr parseInternal(const envoy::api::v2::HeaderValueOption& header_value_option) {
  const std::string& format = header_value_option.header().value();
  const bool append = PROTOBUF_GET_WRAPPED_OR_DEFAULT(header_value_option, append, true);

  if (format.find("%") == 0) {
    const size_t last_occ_pos = format.rfind("%");
    if (last_occ_pos == std::string::npos || last_occ_pos <= 1) {
      throw EnvoyException(fmt::format("Incorrect header configuration. Expected variable format "
                                       "%<variable_name>%, actual format {}",
                                       format));
    }
    return HeaderFormatterPtr{
        new RequestInfoHeaderFormatter(format.substr(1, last_occ_pos - 1), append)};
  } else {
    return HeaderFormatterPtr{new PlainHeaderFormatter(format, append)};
  }
}

} // namespace

void HeaderParserBase::addHeaders(Http::HeaderMap& headers,
                                  const AccessLog::RequestInfo& request_info) const {
  for (const auto& formatter : header_formatters_) {
    if (formatter.second->append()) {
      headers.addReferenceKey(formatter.first, formatter.second->format(request_info));
    } else {
      headers.setReferenceKey(formatter.first, formatter.second->format(request_info));
    }
  }
}

void HeaderParserBase::setHeadersToAdd(
    const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers) {

  for (const auto& header_value_option : headers) {
    HeaderFormatterPtr header_formatter = parseInternal(header_value_option);

    header_formatters_.push_back(
        {Http::LowerCaseString(header_value_option.header().key()), std::move(header_formatter)});
  }
}

RequestHeaderParserPtr RequestHeaderParser::parse(
    const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers) {
  RequestHeaderParserPtr request_header_parser(new RequestHeaderParser());

  request_header_parser->setHeadersToAdd(headers);

  return request_header_parser;
}

void RequestHeaderParser::evaluateRequestHeaders(Http::HeaderMap& headers,
                                                 const AccessLog::RequestInfo& request_info) const {
  addHeaders(headers, request_info);
}

ResponseHeaderParserPtr ResponseHeaderParser::parse(
    const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers_to_add,
    const Protobuf::RepeatedPtrField<ProtobufTypes::String>& headers_to_remove) {
  ResponseHeaderParserPtr response_header_parser(new ResponseHeaderParser());

  response_header_parser->setHeadersToAdd(headers_to_add);

  for (const std::string& header : headers_to_remove) {
    response_header_parser->headers_to_remove_.emplace_back(Http::LowerCaseString(header));
  }

  return response_header_parser;
}

void ResponseHeaderParser::evaluateResponseHeaders(
    Http::HeaderMap& headers, const AccessLog::RequestInfo& request_info) const {
  addHeaders(headers, request_info);

  for (const auto& header : headers_to_remove_) {
    headers.remove(header);
  }
}

} // namespace Router
} // namespace Envoy
