#pragma once

#include "envoy/common/time.h"
#include "envoy/http/header_map.h"

#include "common/http/headers.h"

#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

using OptionalDuration = absl::optional<SystemTime::duration>;

// According to: https://httpwg.org/specs/rfc7234.html#cache-request-directive
struct RequestCacheControl {
  RequestCacheControl() = default;
  explicit RequestCacheControl(absl::string_view cache_control_header);

  // must_validate is true if 'no-cache' directive is present
  // A cached response must not be served without successful validation with the origin
  bool must_validate_ = false;

  // The response to this request must not be cached (stored)
  bool no_store_ = false;

  // 'no-transform' directive is not used now
  // No transformations should be done to the response of this request, as defined by:
  // https://httpwg.org/specs/rfc7230.html#message.transformations
  bool no_transform_ = false;

  // 'only-if-cached' directive is not used now
  // The request should be satisfied using a cached response, or respond with 504 (Gateway Error)
  bool only_if_cached_ = false;

  // The client is unwilling to receive a cached response whose age exceeds the max-age
  OptionalDuration max_age_;

  // The client is unwilling to received a cached response that satisfies:
  //   expiration_time - now < min-fresh
  OptionalDuration min_fresh_;

  // The client is willing to receive a stale response that satisfies:
  //   now - expiration_time < max-stale
  // If max-stale has no value then the client is willing to receive any stale response
  OptionalDuration max_stale_;
};

// According to: https://httpwg.org/specs/rfc7234.html#cache-response-directive
struct ResponseCacheControl {
  ResponseCacheControl() = default;
  explicit ResponseCacheControl(absl::string_view cache_control_header);

  // must_validate is true if 'no-cache' directive is present; arguments are ignored for now
  // This response must not be used to satisfy subsequent requests without successful validation
  // with the origin
  bool must_validate_ = false;

  // no_store is true if any of 'no-store' or 'private' directives is present.
  // 'private' arguments are ignored for now so it is equivalent to 'no-store'
  // This response must not be cached (stored)
  bool no_store_ = false;

  // 'no-transform' directive is not used now
  // No transformations should be done to this response , as defined by:
  // https://httpwg.org/specs/rfc7230.html#message.transformations
  bool no_transform_ = false;

  // no_stale is true if any of 'must-revalidate' or 'proxy-revalidate' directives is present
  // This response must not be served stale without successful validation with the origin
  bool no_stale_ = false;

  // 'public' directive is not used now
  // This response may be stored, even if the response would normally be non-cacheable or cacheable
  // only within a private cache, see:
  // https://httpwg.org/specs/rfc7234.html#cache-response-directive.public
  bool is_public_ = false;

  // max_age is set if to 's-maxage' if present, if not it is set to 'max-age' if present.
  // Indicates the maximum time after which this response will be considered stale
  OptionalDuration max_age_;
};

bool operator==(const RequestCacheControl& lhs, const RequestCacheControl& rhs);
bool operator==(const ResponseCacheControl& lhs, const ResponseCacheControl& rhs);

class CacheHeadersUtils {
public:
  // Parses header_entry as an HTTP time. Returns SystemTime() if
  // header_entry is null or malformed.
  static SystemTime httpTime(const Http::HeaderEntry* header_entry);

  /**
   * Read a leading positive decimal integer value and advance "*str" past the
   * digits read. If overflow occurs, or no digits exist, return
   * absl::nullopt without advancing "*str".
   */
  static absl::optional<uint64_t> readAndRemoveLeadingDigits(absl::string_view& str);
};

class VaryHeader {
public:
  // Checks if the HeaderEntry contains a valid value in the Vary header.
  static bool isAllowed(const Http::ResponseHeaderMap& headers);

  // Checks if the HeaderEntry doesn't contain a Vary header or if it's empty.
  static bool noVary(const Http::ResponseHeaderMap& headers);
  static bool noVary(const Http::ResponseHeaderMapPtr& headers);

  // Creates a single string combining the values of the varied headers from entry_headers.
  static std::string createVaryKey(const Http::HeaderEntry* vary_header,
                                   const std::vector<const Http::HeaderEntry*>& entry_headers);

  // Parses the header names that are in the Vary header value. If the header value is malformed,
  // returns {}.
  static std::vector<std::string> parseHeaderValue(const Http::HeaderEntry* vary_header);

  // Returns a vector with the headers that can be varied from the request.
  static std::vector<const Http::HeaderEntry*>
  possibleVariedHeaders(const Http::RequestHeaderMap& request_headers);

private:
  // Parses the allowlist of header values that can be used to create varied responses.
  static absl::flat_hash_set<std::string> parseAllowlist();

  // Set containing the headers that can be used to vary responses.
  inline static const absl::flat_hash_set<std::string> allowed_headers_ = parseAllowlist();

  // Checks if value is a valid header name.
  static bool isValidHeaderName(const std::string value);
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
