#include "common/router/header_parser.h"

#include <ctype.h>

#include <string>

#include "common/common/assert.h"
#include "common/protobuf/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Router {

namespace {

enum class ParserState {
  Literal,                   // processing literal data
  VariableName,              // consuming a %VAR% name
  ExpectArray,               // expect starting [ in %VAR([...])%
  ExpectString,              // expect starting " in array of strings
  String,                    // consuming an array element string
  ExpectArrayDelimiterOrEnd, // expect array delimiter (,) or end of array (])
  ExpectArgsEnd,             // expect closing ) in %VAR(...)%
  ExpectVariableEnd          // expect closing % in %VAR(...)%
};

std::string unescape(absl::string_view sv) { return absl::StrReplaceAll(sv, {{"%%", "%"}}); }

// Implements a state machine to parse custom headers. Each character of the custom header format
// is either literal text (with % escaped as %%) or part of a %VAR% or %VAR(["args"])% expression.
// The statement machine does minimal validation of the arguments (if any) and does not know the
// names of valid variables. Interpretation of the variable name and arguments is delegated to
// RequestInfoHeaderFormatter.
HeaderFormatterPtr parseInternal(const envoy::api::v2::HeaderValueOption& header_value_option) {
  const bool append = PROTOBUF_GET_WRAPPED_OR_DEFAULT(header_value_option, append, true);

  absl::string_view format(header_value_option.header().value());
  if (format.empty()) {
    return HeaderFormatterPtr{new PlainHeaderFormatter("", append)};
  }

  std::vector<HeaderFormatterPtr> formatters;

  size_t pos = 0, start = 0;
  ParserState state = ParserState::Literal;
  do {
    const char ch = format[pos];
    const bool has_next_ch = (pos + 1) < format.size();

    switch (state) {
    case ParserState::Literal:
      // Searching for start of %VARIABLE% expression.
      if (ch != '%') {
        break;
      }

      if (!has_next_ch) {
        throw EnvoyException(
            fmt::format("Invalid header configuration. Un-escaped % at position {}", pos));
      }

      if (format[pos + 1] == '%') {
        // Escaped %, skip next character.
        pos++;
        break;
      }

      // Un-escaped %: start of variable name. Create a formatter for preceding characters, if
      // any.
      state = ParserState::VariableName;
      if (pos > start) {
        absl::string_view literal = format.substr(start, pos - start);
        formatters.emplace_back(new PlainHeaderFormatter(unescape(literal), append));
      }
      start = pos + 1;
      break;

    case ParserState::VariableName:
      // Consume "VAR" from "%VAR%" or "%VAR(...)%"
      if (ch == '%') {
        // Found complete variable name, add formatter.
        formatters.emplace_back(
            new RequestInfoHeaderFormatter(format.substr(start, pos - start), append));
        start = pos + 1;
        state = ParserState::Literal;
        break;
      }

      if (ch == '(') {
        // Variable with arguments, search for start of arg array.
        state = ParserState::ExpectArray;
      }
      break;

    case ParserState::ExpectArray:
      // Skip over whitespace searching for the start of JSON array args.
      if (ch == '[') {
        // Search for first argument string
        state = ParserState::ExpectString;
      } else if (!isspace(ch)) {
        throw EnvoyException(fmt::format(
            "Invalid header configuration. Expecting JSON array of arguments after '{}', but "
            "found '{}'",
            absl::StrCat(format.substr(start, pos - start)), ch));
      }
      break;

    case ParserState::ExpectArrayDelimiterOrEnd:
      // Skip over whitespace searching for a comma or close bracket.
      if (ch == ',') {
        state = ParserState::ExpectString;
      } else if (ch == ']') {
        state = ParserState::ExpectArgsEnd;
      } else if (!isspace(ch)) {
        throw EnvoyException(fmt::format(
            "Invalid header configuration. Expecting ',', ']', or whitespace after '{}', but "
            "found '{}'",
            absl::StrCat(format.substr(start, pos - start)), ch));
      }
      break;

    case ParserState::ExpectString:
      // Skip over whitespace looking for the starting quote of a JSON string.
      if (ch == '"') {
        state = ParserState::String;
      } else if (!isspace(ch)) {
        throw EnvoyException(fmt::format(
            "Invalid header configuration. Expecting '\"' or whitespace after '{}', but found '{}'",
            absl::StrCat(format.substr(start, pos - start)), ch));
      }
      break;

    case ParserState::String:
      // Consume a JSON string (ignoring backslash-escaped chars).
      if (ch == '\\') {
        if (!has_next_ch) {
          throw EnvoyException(fmt::format(
              "Invalid header configuration. Un-terminated backslash in JSON string after '{}'",
              absl::StrCat(format.substr(start, pos - start))));
        }

        // Skip escaped char.
        pos++;
      } else if (ch == '"') {
        state = ParserState::ExpectArrayDelimiterOrEnd;
      }
      break;

    case ParserState::ExpectArgsEnd:
      // Search for the closing paren of a %VAR(...)% expression.
      if (ch == ')') {
        state = ParserState::ExpectVariableEnd;
      } else if (!isspace(ch)) {
        throw EnvoyException(fmt::format(
            "Invalid header configuration. Expecting ')' or whitespace after '{}', but found '{}'",
            absl::StrCat(format.substr(start, pos - start)), ch));
      }
      break;

    case ParserState::ExpectVariableEnd:
      // Search for closing % of a %VAR(...)% expression
      if (ch == '%') {
        formatters.emplace_back(
            new RequestInfoHeaderFormatter(format.substr(start, pos - start), append));
        start = pos + 1;
        state = ParserState::Literal;
        break;
      }

      if (!isspace(ch)) {
        throw EnvoyException(fmt::format(
            "Invalid header configuration. Expecting '%' or whitespace after '{}', but found '{}'",
            absl::StrCat(format.substr(start, pos - start)), ch));
      }
      break;

    default:
      NOT_REACHED;
    }
  } while (++pos < format.size());

  if (state != ParserState::Literal) {
    // Parsing terminated mid-variable.
    throw EnvoyException(
        fmt::format("Invalid header configuration. Un-terminated variable expression '{}'",
                    absl::StrCat(format.substr(start, pos - start))));
  }

  if (pos > start) {
    // Trailing constant data.
    absl::string_view literal = format.substr(start, pos - start);
    formatters.emplace_back(new PlainHeaderFormatter(unescape(literal), append));
  }

  ASSERT(formatters.size() > 0);

  if (formatters.size() == 1) {
    return std::move(formatters[0]);
  }

  return HeaderFormatterPtr{new CompoundHeaderFormatter(std::move(formatters), append)};
}

} // namespace

HeaderParserPtr HeaderParser::configure(
    const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers_to_add) {
  HeaderParserPtr header_parser(new HeaderParser());

  for (const auto& header_value_option : headers_to_add) {
    HeaderFormatterPtr header_formatter = parseInternal(header_value_option);

    header_parser->headers_to_add_.emplace_back(
        Http::LowerCaseString(header_value_option.header().key()), std::move(header_formatter));
  }

  return header_parser;
}

HeaderParserPtr HeaderParser::configure(
    const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers_to_add,
    const Protobuf::RepeatedPtrField<ProtobufTypes::String>& headers_to_remove) {
  HeaderParserPtr header_parser = configure(headers_to_add);

  for (const auto& header : headers_to_remove) {
    header_parser->headers_to_remove_.emplace_back(header);
  }

  return header_parser;
}

void HeaderParser::evaluateHeaders(Http::HeaderMap& headers,
                                   const RequestInfo::RequestInfo& request_info) const {
  for (const auto& formatter : headers_to_add_) {
    const std::string value = formatter.second->format(request_info);
    if (!value.empty()) {
      if (formatter.second->append()) {
        headers.addReferenceKey(formatter.first, value);
      } else {
        headers.setReferenceKey(formatter.first, value);
      }
    }
  }

  for (const auto& header : headers_to_remove_) {
    headers.remove(header);
  }
}

} // namespace Router
} // namespace Envoy
