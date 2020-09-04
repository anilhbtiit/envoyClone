#pragma once

#include "common/protobuf/utility.h"

#include "extensions/filters/http/cache/http_cache.h"

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

// Example cache backend that never evicts. Not suitable for production use.
class SimpleHttpCache : public HttpCache {
private:
  struct Entry {
    Http::ResponseHeaderMapPtr response_headers_;
    std::string body_;
  };

  // Looks for a response that has been varied. Only called from lookup.
  Entry varyLookup(const LookupRequest& request,
                   const Http::ResponseHeaderMapPtr& response_headers);

public:
  // HttpCache
  LookupContextPtr makeLookupContext(LookupRequest&& request) override;
  InsertContextPtr makeInsertContext(LookupContextPtr&& lookup_context) override;
  void updateHeaders(const LookupContext& lookup_context,
                     const Http::ResponseHeaderMap& response_headers) override;
  CacheInfo cacheInfo() const override;

  Entry lookup(const LookupRequest& request);
  void insert(const Key& key, Http::ResponseHeaderMapPtr&& response_headers, std::string&& body);

  // Inserts a response that has been varied on certain headers.
  void varyInsert(const Key& request_key, Http::ResponseHeaderMapPtr&& response_headers,
                  std::string&& body, const Http::RequestHeaderMap& request_vary_headers);

  absl::Mutex mutex_;
  absl::flat_hash_map<Key, Entry, MessageUtil, MessageUtil> map_ ABSL_GUARDED_BY(mutex_);
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
