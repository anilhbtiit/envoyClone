#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/filter/http/cache/v2/cache.pb.h"

#include "common/common/logger.h"

#include "extensions/filters/http/cache/http_cache.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

/**
 * A filter that caches responses and attempts to satisfy requests from cache.
 * It also inherits from std::enable_shared_from_this so it can pass shared_ptrs to async methods,
 * to ensure that it doesn't get destroyed before they complete.
 */
class CacheFilter;
using CacheFilterSharedPtr = std::shared_ptr<CacheFilter>;
class CacheFilter : public Http::PassThroughFilter,
                    public Logger::Loggable<Logger::Id::cache_filter>,
                    public std::enable_shared_from_this<CacheFilter> {
public:
  // Throws ProtoValidationException if no registered HttpCacheFactory for config.name.
  static CacheFilterSharedPtr make(const envoy::config::filter::http::cache::v2::Cache& config,
                                   const std::string& stats_prefix, Stats::Scope& scope,
                                   TimeSource& time_source) {
    // Can't use make_shared due to private constructor.
    return std::shared_ptr<CacheFilter>(new CacheFilter(config, stats_prefix, scope, time_source));
  }
  // Http::StreamFilterBase
  void onDestroy() override;
  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override;

private:
  // Throws EnvoyException if no registered HttpCacheFactory for config.name.
  // Constructor is private to enforce enable_shared_from_this's requirement that this must be owned
  // by a shared_ptr.
  CacheFilter(const envoy::config::filter::http::cache::v2::Cache& config,
              const std::string& stats_prefix, Stats::Scope& scope, TimeSource& time_source);

  void getBody();
  void onOkHeaders(Http::HeaderMapPtr&& headers, std::vector<AdjustedByteRange>&& response_ranges,
                   uint64_t content_length, bool has_trailers);
  void onUnusableHeaders();
  void onBody(Buffer::InstancePtr&& body);
  void onTrailers(Http::HeaderMapPtr&& trailers);
  static void onHeadersAsync(const CacheFilterSharedPtr& self, LookupResult&& result);
  static void onBodyAsync(const CacheFilterSharedPtr& self, Buffer::InstancePtr&& body);
  static void onTrailersAsync(const CacheFilterSharedPtr& self, Http::HeaderMapPtr&& trailers);
  void post(std::function<void()> f) const;

  // These don't require private access, but are members per envoy convention.
  static bool isCacheableRequest(Http::HeaderMap& headers);
  static bool isCacheableResponse(Http::HeaderMap& headers);
  static HttpCache& getCache(const envoy::config::filter::http::cache::v2::Cache& config);

  TimeSource& time_source_;
  HttpCache& cache_;
  LookupContextPtr lookup_;
  InsertContextPtr insert_;

  // Tracks what body bytes still need to be read from the cache. This is
  // currently only one Range, but will expand when full range support is added. Initialized by
  // onOkHeaders.
  std::vector<AdjustedByteRange> remaining_body_;

  // True if the response has trailers.
  // TODO(toddmgreer) cache trailers.
  bool response_has_trailers_;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
