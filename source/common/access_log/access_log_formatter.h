#pragma once

#include <functional>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/common/time.h"
#include "envoy/request_info/request_info.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace AccessLog {

/**
 * Access log format parser.
 */
class AccessLogFormatParser {
public:
  static std::vector<FormatterPtr> parse(const std::string& format);

private:
  static void parseCommandHeader(const std::string& token, const size_t start,
                                 std::string& main_header, std::string& alternative_header,
                                 absl::optional<size_t>& max_length);
  /**
   * General parse command utility. Will parse token from start position. Token is expected to end
   * with ')'. An optional ":max_length" may be specified after the closing ')' char. Token may
   * contain multiple values separated by "seperator" string. First value will be populated in
   * "main" and any additional sub values will be set in the vector "subs". For example token of:
   * "com.test.my_filter:test_object:inner_key):100" with separator of ":" will set the following:
   * - main: com.test.my_filter
   * - subs: {test_object, inner_key}
   * - max_length: 100
   *
   * @param token the token to parse
   * @param start the index to start parsing from
   * @param seperator seperator between values
   * @param main the first value
   * @param subs any additional values
   * @param max_length optional max_length will be populated if specified
   */
  static void parseCommand(const std::string& token, const size_t start,
                           const std::string& separator, std::string& main,
                           std::vector<std::string>& subs, absl::optional<size_t>& max_length);
};

/**
 * Util class for access log format.
 */
class AccessLogFormatUtils {
public:
  static FormatterPtr defaultAccessLogFormatter();
  static const std::string& protocolToString(const absl::optional<Http::Protocol>& protocol);
  static std::string durationToString(const absl::optional<std::chrono::nanoseconds>& time);

private:
  AccessLogFormatUtils();

  static const std::string DEFAULT_FORMAT;
};

/**
 * Composite formatter implementation.
 */
class FormatterImpl : public Formatter {
public:
  FormatterImpl(const std::string& format);

  // Formatter::format
  std::string format(const Http::HeaderMap& request_headers,
                     const Http::HeaderMap& response_headers,
                     const RequestInfo::RequestInfo& request_info) const override;

private:
  std::vector<FormatterPtr> formatters_;
};

/**
 * Formatter for string literal. It ignores headers and request info and returns string by which it
 * was initialized.
 */
class PlainStringFormatter : public Formatter {
public:
  PlainStringFormatter(const std::string& str);

  // Formatter::format
  std::string format(const Http::HeaderMap&, const Http::HeaderMap&,
                     const RequestInfo::RequestInfo&) const override;

private:
  std::string str_;
};

class HeaderFormatter {
public:
  HeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                  absl::optional<size_t> max_length);

  std::string format(const Http::HeaderMap& headers) const;

private:
  Http::LowerCaseString main_header_;
  Http::LowerCaseString alternative_header_;
  absl::optional<size_t> max_length_;
};

/**
 * Formatter based on request header.
 */
class RequestHeaderFormatter : public Formatter, HeaderFormatter {
public:
  RequestHeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                         absl::optional<size_t> max_length);

  // Formatter::format
  std::string format(const Http::HeaderMap& request_headers, const Http::HeaderMap&,
                     const RequestInfo::RequestInfo&) const override;
};

/**
 * Formatter based on the response header.
 */
class ResponseHeaderFormatter : public Formatter, HeaderFormatter {
public:
  ResponseHeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                          absl::optional<size_t> max_length);

  // Formatter::format
  std::string format(const Http::HeaderMap&, const Http::HeaderMap& response_headers,
                     const RequestInfo::RequestInfo&) const override;
};

/**
 * Formatter based on the RequestInfo field.
 */
class RequestInfoFormatter : public Formatter {
public:
  RequestInfoFormatter(const std::string& field_name);

  // Formatter::format
  std::string format(const Http::HeaderMap&, const Http::HeaderMap&,
                     const RequestInfo::RequestInfo& request_info) const override;

private:
  std::function<std::string(const RequestInfo::RequestInfo&)> field_extractor_;
};

/**
 * Base formatter for formatting Metadata objects
 */
class MetadataFormatter {
public:
  MetadataFormatter(const std::string& filter_namespace, const std::vector<std::string>& path,
                    absl::optional<size_t> max_length);

  std::string format(const envoy::api::v2::core::Metadata& metadata) const;

private:
  std::string filter_namespace_;
  std::vector<std::string> path_;
  absl::optional<size_t> max_length_;
};

/**
 * Formatter based on the DynamicMetadata from RequestInfo.
 */
class DynamicMetadataFormatter : public Formatter, MetadataFormatter {
public:
  DynamicMetadataFormatter(const std::string& filter_namespace,
                           const std::vector<std::string>& path, absl::optional<size_t> max_length);

  // Formatter::format
  std::string format(const Http::HeaderMap&, const Http::HeaderMap&,
                     const RequestInfo::RequestInfo& request_info) const override;
};

} // namespace AccessLog
} // namespace Envoy
