#include "extensions/filters/http/cache/cacheability_utils.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    authorization_handle(Http::CustomHeaders::get().Authorization);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    cache_control_handle(Http::CustomHeaders::get().CacheControl);

// As defined by:
// https://tools.ietf.org/html/rfc7231#section-6.1,
// https://tools.ietf.org/html/rfc7538#section-3,
// https://tools.ietf.org/html/rfc7725#section-3
// TODO(yosrym93): the list of cacheable status codes should be configurable
const absl::flat_hash_set<absl::string_view> CacheabilityUtils::cacheable_status_codes_ = {
    "200", "203", "204", "206", "300", "301", "308", "404", "405", "410", "414", "451", "501"};

bool CacheabilityUtils::isCacheableRequest(const Http::RequestHeaderMap& headers) {
  const absl::string_view method = headers.getMethodValue();
  const absl::string_view forwarded_proto = headers.getForwardedProtoValue();
  const Http::HeaderValues& header_values = Http::Headers::get();
  // TODO(toddmgreer): Also serve HEAD requests from cache.
  // TODO(toddmgreer): Check all the other cache-related headers.
  return headers.Path() && headers.Host() && !headers.getInline(authorization_handle.handle()) &&
         (method == header_values.MethodValues.Get) &&
         (forwarded_proto == header_values.SchemeValues.Http ||
          forwarded_proto == header_values.SchemeValues.Https);
}

bool CacheabilityUtils::isCacheableResponse(const Http::ResponseHeaderMap& headers) {
  absl::string_view cache_control = headers.getInlineValue(cache_control_handle.handle());
  ResponseCacheControl response_cache_control(cache_control);
  bool cacheable_status = cacheable_status_codes_.contains(headers.getStatusValue());

  // Only cache responses with explicit validation data, either:
  //    max-age or s-maxage cache-control directives with date header
  //    expires header
  // TODO(yosrym93): If the response has no date header inject date metadata
  bool has_validation_data = (headers.Date() && response_cache_control.max_age_.has_value()) ||
                             headers.get(Http::Headers::get().Expires);

  return !response_cache_control.no_store_ && cacheable_status && has_validation_data;
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy