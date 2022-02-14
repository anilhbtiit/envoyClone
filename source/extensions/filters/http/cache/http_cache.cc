#include "source/extensions/filters/http/cache/http_cache.h"

#include <algorithm>
#include <ostream>
#include <vector>

#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"

#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/cache/cache_custom_headers.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

LookupRequest::LookupRequest(const Http::RequestHeaderMap& request_headers, SystemTime timestamp,
                             const VaryAllowList& vary_allow_list)
    : request_headers_(Http::createHeaderMap<Http::RequestHeaderMapImpl>(request_headers)),
      vary_allow_list_(vary_allow_list), timestamp_(timestamp) {
  // These ASSERTs check prerequisites. A request without these headers can't be looked up in cache;
  // CacheFilter doesn't create LookupRequests for such requests.
  ASSERT(request_headers.Path(), "Can't form cache lookup key for malformed Http::RequestHeaderMap "
                                 "with null Path.");
  ASSERT(request_headers.Host(), "Can't form cache lookup key for malformed Http::RequestHeaderMap "
                                 "with null Host.");
  absl::string_view scheme = Http::Utility::getScheme(request_headers);
  const auto& scheme_values = Http::Headers::get().SchemeValues;
  ASSERT(scheme == scheme_values.Http || scheme == scheme_values.Https);

  initializeRequestCacheControl(request_headers);
  // TODO(toddmgreer): Let config determine whether to include scheme, host, and
  // query params.

  // TODO(toddmgreer): get cluster name.
  key_.set_cluster_name("cluster_name_goes_here");
  key_.set_host(std::string(request_headers.getHostValue()));
  key_.set_path(std::string(request_headers.getPathValue()));
  if (scheme == scheme_values.Http) {
    key_.set_scheme(Key::HTTP);
  } else if (scheme == "https") {
    key_.set_scheme(Key::HTTPS);
  }
}

// Unless this API is still alpha, calls to stableHashKey() must always return
// the same result, or a way must be provided to deal with a complete cache
// flush.
size_t stableHashKey(const Key& key) { return MessageUtil::hash(key); }

void LookupRequest::initializeRequestCacheControl(const Http::RequestHeaderMap& request_headers) {
  const absl::string_view cache_control =
      request_headers.getInlineValue(CacheCustomHeaders::requestCacheControl());
  const absl::string_view pragma = request_headers.getInlineValue(CacheCustomHeaders::pragma());

  if (!cache_control.empty()) {
    request_cache_control_ = RequestCacheControl(cache_control);
  } else {
    // According to: https://httpwg.org/specs/rfc7234.html#header.pragma,
    // when Cache-Control header is missing, "Pragma:no-cache" is equivalent to
    // "Cache-Control:no-cache". Any other directives are ignored.
    request_cache_control_.must_validate_ = RequestCacheControl(pragma).must_validate_;
  }
}

bool LookupRequest::requiresValidation(const Http::ResponseHeaderMap& response_headers,
                                       SystemTime::duration response_age) const {
  // TODO(yosrym93): Store parsed response cache-control in cache instead of parsing it on every
  // lookup.
  const absl::string_view cache_control =
      response_headers.getInlineValue(CacheCustomHeaders::responseCacheControl());
  const ResponseCacheControl response_cache_control(cache_control);

  const bool request_max_age_exceeded = request_cache_control_.max_age_.has_value() &&
                                        request_cache_control_.max_age_.value() < response_age;
  if (response_cache_control.must_validate_ || request_cache_control_.must_validate_ ||
      request_max_age_exceeded) {
    // Either the request or response explicitly require validation, or a request max-age
    // requirement is not satisfied.
    return true;
  }

  // CacheabilityUtils::isCacheableResponse(..) guarantees that any cached response satisfies this.
  ASSERT(response_cache_control.max_age_.has_value() ||
             (response_headers.getInline(CacheCustomHeaders::expires()) && response_headers.Date()),
         "Cache entry does not have valid expiration data.");

  SystemTime::duration freshness_lifetime;
  if (response_cache_control.max_age_.has_value()) {
    freshness_lifetime = response_cache_control.max_age_.value();
  } else {
    const SystemTime expires_value =
        CacheHeadersUtils::httpTime(response_headers.getInline(CacheCustomHeaders::expires()));
    const SystemTime date_value = CacheHeadersUtils::httpTime(response_headers.Date());
    freshness_lifetime = expires_value - date_value;
  }

  if (response_age > freshness_lifetime) {
    // Response is stale, requires validation if
    // the response does not allow being served stale,
    // or the request max-stale directive does not allow it.
    const bool allowed_by_max_stale =
        request_cache_control_.max_stale_.has_value() &&
        request_cache_control_.max_stale_.value() > response_age - freshness_lifetime;
    return response_cache_control.no_stale_ || !allowed_by_max_stale;
  } else {
    // Response is fresh, requires validation only if there is an unsatisfied min-fresh requirement.
    const bool min_fresh_unsatisfied =
        request_cache_control_.min_fresh_.has_value() &&
        request_cache_control_.min_fresh_.value() > freshness_lifetime - response_age;
    return min_fresh_unsatisfied;
  }
}

LookupResult LookupRequest::makeLookupResult(Http::ResponseHeaderMapPtr&& response_headers,
                                             ResponseMetadata&& metadata, uint64_t content_length,
                                             bool has_trailers) const {
  // TODO(toddmgreer): Implement all HTTP caching semantics.
  ASSERT(response_headers);
  LookupResult result;

  // Assumption: Cache lookup time is negligible. Therefore, now == timestamp_
  const Seconds age =
      CacheHeadersUtils::calculateAge(*response_headers, metadata.response_time_, timestamp_);
  response_headers->setInline(CacheCustomHeaders::age(), std::to_string(age.count()));

  result.cache_entry_status_ = requiresValidation(*response_headers, age)
                                   ? CacheEntryStatus::RequiresValidation
                                   : CacheEntryStatus::Ok;
  result.headers_ = std::move(response_headers);
  result.content_length_ = content_length;
  result.range_details_ = RangeUtils::createRangeDetails(requestHeaders(), content_length);
  result.has_trailers_ = has_trailers;

  return result;
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
