#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_context.h"
#include "extensions/filters/http/cache/hazelcast_http_cache/config_util.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

using Envoy::Protobuf::util::MessageDifferencer;

UnifiedLookupContext::UnifiedLookupContext(HazelcastHttpCache& cache, LookupRequest&& request)
    : HazelcastLookupContextBase(cache, std::move(request)) {}

void UnifiedLookupContext::getHeaders(LookupHeadersCallback&& cb) {
  ENVOY_LOG(debug, "Looking up unified response with key {}u", variant_hash_key_);
  try {
    response_ = hz_cache_.getResponse(variant_hash_key_);
  } catch (HazelcastClientOfflineException e){
    ENVOY_LOG(warn, "Hazelcast cluster connection is lost! Aborting lookups and insertions"
                    "until the connection is restored...");
    abort_insertion_ = true;
    cb(LookupResult{});
    return;
  }
  if (response_) {
    ENVOY_LOG(debug, "Found unified response for key {}u, "
                     "body size = {}", variant_hash_key_, response_->body().length());
    if (!MessageDifferencer::Equals(response_->header().variantKey(), variantKey())) {
      // As cache filter denotes, a secondary check other than the hash key
      // is performed here. If a different response is found with the same
      // hash (probably on hash collisions), the new response is denied to
      // be cached and the old one remains.
      ENVOY_LOG(debug, "Keys mismatched for hash {}u. "
                       "Aborting lookup & insertion", variant_hash_key_);
      abort_insertion_ = true;
      cb(LookupResult{});
      return;
    }
    cb(lookup_request_.makeLookupResult(std::move(response_->header().headerMap()),
                                        response_->body().length()));
  } else {
    ENVOY_LOG(debug, "Didn't find unified response for key {}u", variant_hash_key_);
    // Unlike DIVIDED mode, lock is not tried to be acquired before insertion.
    // Instead, when putting a unified response into cache, putIfAbsent is called
    // and hence only one insertion is performed. Cost for the creation of the
    // unified entry (simultaneously) by multiple contexts is preferred
    // over locking mechanism here.
    cb(LookupResult{});
  }
}

void UnifiedLookupContext::getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb) {
  ENVOY_LOG(debug, "Getting unified body (total length = {}) with range from {} to {}",
      response_->body().length(), range.begin(), range.end());
  ASSERT(response_ && !abort_insertion_);
  ASSERT(range.end() <= response_->body().length());
  hazelcast::byte* data = response_->body().begin() + range.begin();
  cb(std::make_unique<Buffer::OwnedImpl>(data, range.length()));
}

UnifiedInsertContext::UnifiedInsertContext(LookupContext& lookup_context,
    HazelcastHttpCache& cache) : HazelcastInsertContextBase(lookup_context, cache) {}

void UnifiedInsertContext::insertHeaders(const Http::ResponseHeaderMap& response_headers,
                                         bool end_stream) {
  if (abort_insertion_) {
    return;
  }
  ASSERT(!committed_end_stream_);
  header_map_ = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers);
  if (end_stream) {
    flushEntry();
  }
}

void UnifiedInsertContext::insertBody(const Buffer::Instance& chunk,
                                      InsertCallback ready_for_next_chunk, bool end_stream) {
  if (abort_insertion_) {
    if (ready_for_next_chunk) {
      ready_for_next_chunk(false);
    }
    return;
  }
  ASSERT(!committed_end_stream_);
  size_t buffer_length = buffer_vector_.size();
  size_t allowed_size = max_body_size_ - buffer_length;
  if (allowed_size > chunk.length()) {
    buffer_vector_.resize(buffer_length + chunk.length());
    chunk.copyOut(0, chunk.length(), buffer_vector_.data() + buffer_length);
  } else {
    // Store the body copied until now and abort the further attempted.
    buffer_vector_.resize(max_body_size_);
    chunk.copyOut(0, allowed_size, buffer_vector_.data() + buffer_length);
    flushEntry();
    ready_for_next_chunk(false);
    return;
  }

  if (end_stream) {
    flushEntry();
  } else if (ready_for_next_chunk) {
    ready_for_next_chunk(true);
  }
}

void UnifiedInsertContext::flushEntry() {
  ASSERT(!abort_insertion_);
  ASSERT(!committed_end_stream_);
  ENVOY_LOG(debug, "Inserting unified entry if absent with key {}u", variant_hash_key_);
  committed_end_stream_ = true;

  // Versions are not necessary for unified entries. Hence passing arbitrary 0 here.
  HazelcastHeaderEntry header(std::move(header_map_), std::move(variant_key_),
      buffer_vector_.size(), 0);
  HazelcastBodyEntry body(variant_hash_key_, std::move(buffer_vector_), 0);

  HazelcastResponseEntry entry(std::move(header), std::move(body));
  try {
    hz_cache_.putResponseIfAbsent(variant_hash_key_, entry);
  } catch (HazelcastClientOfflineException e) {
    ENVOY_LOG(warn, "Hazelcast cluster connection is lost!");  }
}

DividedLookupContext::DividedLookupContext(HazelcastHttpCache& cache, LookupRequest&& request)
    : HazelcastLookupContextBase(cache, std::move(request)),
    body_partition_size_(cache.bodySizePerEntry()) {};

void DividedLookupContext::getHeaders(LookupHeadersCallback&& cb) {
  ENVOY_LOG(debug, "Looking up divided header with key {}u", variant_hash_key_);
  HazelcastHeaderPtr header_entry;
  try {
    header_entry = hz_cache_.getHeader(variant_hash_key_);
  } catch (HazelcastClientOfflineException e){
    ENVOY_LOG(warn, "Hazelcast cluster connection is lost! Aborting lookups and insertions"
                    "until the connection is restored...");
    abort_insertion_ = true;
    cb(LookupResult{});
    return;
  }
  if (header_entry) {
    ENVOY_LOG(debug, "Found divided response for key {}u, version {}, body size = {}",
        variant_hash_key_, header_entry->version(), header_entry->bodySize());
    if (!MessageDifferencer::Equals(header_entry->variantKey(), variantKey())) {
      // The same logic with UnifiedLookupContext#getHeaders applies.
      ENVOY_LOG(debug, "Keys mismatched for hash {}u. "
                       "Aborting lookup & insertion", variant_hash_key_);
      abort_insertion_ = true;
      cb(LookupResult{});
      return;
    }
    this->total_body_size_ = header_entry->bodySize();
    this->version_ = header_entry->version();
    cb(lookup_request_.makeLookupResult(std::move(header_entry->headerMap()), total_body_size_));
  } else {
    ENVOY_LOG(debug, "Didn't find divided response for key {}u", variant_hash_key_);
    // To prevent multiple insertion contexts to create the same response in the cache,
    // mark only one of them responsible for the insertion using Hazelcast map key locks.
    // If key is not locked, it will be acquired here and only one insertion context
    // created for this lookup will be responsible for the insertion. This is also valid
    // when multiple cache filters from different proxies are connected to the same
    // Hazelcast cluster.
    try {
      abort_insertion_ = !hz_cache_.tryLock(variant_hash_key_);
    } catch (HazelcastClientOfflineException e) {
      ENVOY_LOG(warn, "Hazelcast cluster connection is lost! Aborting lookups and insertions"
                      " until the connection is restored...");
      abort_insertion_ = true;
    }
    cb(LookupResult{});
  }
}

// Hence bodies are stored partially on the cache (see hazelcast_cache_entry.h for details),
// the returning buffer from this function can have a size of at most body_partition_size_.
// The caller (filter) has to check range and make another getBody request if needed.
//
// For instance, for a response of which body is 5 MB length, the cached entries will look
// like the following with 2 MB of body_partition_size_ configured:
//
// <variant_hash(long)> --> HazelcastHeaderEntry(response headers)
//
// <variant_hash(string) + "0"> --> HazelcastBodyEntry(0-2 MB)
// <variant_hash(string) + "1"> --> HazelcastBodyEntry(2-4 MB)
// <variant_hash(string) + "2"> --> HazelcastBodyEntry(4-5 MB)
//
void DividedLookupContext::getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb) {
  ASSERT(range.end() <= total_body_size_);
  ASSERT(!abort_insertion_);

  // Lookup for only one body partition which includes the range.begin().
  uint64_t body_index = range.begin() / body_partition_size_;
  HazelcastBodyPtr body;
  try {
    body = hz_cache_.getBody(variant_hash_key_, body_index);
  } catch (HazelcastClientOfflineException e) {
    ENVOY_LOG(warn, "Hazelcast cluster connection is lost! Aborting lookups and insertions"
                    " until the connection is restored...");
    cb(nullptr);
    return;
  }

  if (body) {
    ENVOY_LOG(debug, "Found divided body with key {}u + \"{}\", version {}, size {}",
        variant_hash_key_, body_index, body->version(),body->length());
    if (body->version() != version_) {
      ENVOY_LOG(debug, "Body version mismatched with header for key {}u at body: {}. "
                       "Aborting lookup and performing cleanup.", variant_hash_key_, body_index);
      hz_cache_.onVersionMismatch(variant_hash_key_, version_, total_body_size_);
      cb(nullptr);
      return;
    }
    uint64_t offset = (range.begin() % body_partition_size_);
    hazelcast::byte* data = body->begin() + offset;
    if (range.end() < (body_index + 1) * body_partition_size_) {
      // No other body partition is needed since this one satisfies the
      // range. Callback with the appropriate body bytes.
      cb(std::make_unique<Buffer::OwnedImpl>(data, range.length()));
    } else {
      // The range requests bytes from the next body partition as well.
      // Callback with the bytes until the end of the current partition.
      cb(std::make_unique<Buffer::OwnedImpl>(data, body->length() - offset));
    }
  } else {
    // Body partition is expected to reside in the cache but lookup is failed.
    ENVOY_LOG(debug, "Found missing body for key {}u at body: {}. Cleaning up response"
                     "with body size: {}", variant_hash_key_, body_index, total_body_size_);
    hz_cache_.onMissingBody(variant_hash_key_, version_, total_body_size_);
    cb(nullptr);
  }
};

DividedInsertContext::DividedInsertContext(LookupContext& lookup_context,
    HazelcastHttpCache& cache) : HazelcastInsertContextBase(lookup_context, cache),
    body_partition_size_(cache.bodySizePerEntry()), version_(createVersion()) {}

void DividedInsertContext::insertHeaders(const Http::ResponseHeaderMap& response_headers,
    bool end_stream) {
  if (abort_insertion_) {
    return;
  }
  ASSERT(!committed_end_stream_);
  header_map_ = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers);
  if (end_stream) {
    flushHeader();
  }
}

// Body insertions in DIVIDED cache mode must be performed over a fixed sized buffer
// hence continuity of the body partitions are ensured. To do this, insertion chunk's
// content is copied into a local buffer every time insertBody is called. And it is
// flushed when it reaches the maximum capacity (body_partition_size_).
void DividedInsertContext::insertBody(const Buffer::Instance& chunk,
    InsertCallback ready_for_next_chunk, bool end_stream) {
  if (abort_insertion_) {
    ENVOY_LOG(debug, "Aborting insertion for the hash key: {}", variant_hash_key_);
    if (ready_for_next_chunk) {
      ready_for_next_chunk(false);
    }
    return;
  }
  ASSERT(!committed_end_stream_);
  uint64_t copied_bytes = 0;
  uint64_t allowed_bytes = max_body_size_ -
                           (body_order_ * body_partition_size_ + buffer_vector_.size());
  uint64_t remaining_bytes = allowed_bytes < chunk.length() ? allowed_bytes : chunk.length();
  bool trimmed = remaining_bytes == allowed_bytes;
  while (remaining_bytes) {
    uint64_t available_bytes = body_partition_size_ - buffer_vector_.size();
    if (available_bytes < remaining_bytes) {
      // This chunk is going to fill the buffer. Copy as much bytes as possible
      // into the buffer, flush the buffer and continue with the remaining bytes.
      copyIntoLocalBuffer(copied_bytes, available_bytes, chunk);
      ASSERT(buffer_vector_.size() == body_partition_size_);
      remaining_bytes -= available_bytes;
      flushBuffer();
    } else {
      // Copy all the bytes starting from chunk[copied_bytes] into buffer. Current
      // buffer can hold the remaining data.
      copyIntoLocalBuffer(copied_bytes, remaining_bytes, chunk);
      break;
    }
  }

  if (end_stream || trimmed) {
    // Header shouldn't be inserted before body insertions are completed.
    // Total body size in the header entry is computed via inserted body partitions.
    flushBuffer();
    flushHeader();
  }
  if (ready_for_next_chunk) {
    ready_for_next_chunk(!trimmed);
  }
}

void DividedInsertContext::copyIntoLocalBuffer(uint64_t& offset, uint64_t& size,
    const Buffer::Instance& source) {
  uint64_t current_size = buffer_vector_.size();
  buffer_vector_.resize(current_size + size);
  source.copyOut(offset, size, buffer_vector_.data() + current_size);
  offset += size;
};

void DividedInsertContext::flushBuffer() {
  ASSERT(!abort_insertion_);
  if (buffer_vector_.size() == 0) {
    return;
  }
  total_body_size_ += buffer_vector_.size();
  HazelcastBodyEntry bodyEntry(hz_cache_.mapKey(variant_hash_key_),
      std::move(buffer_vector_), version_);
  buffer_vector_.clear();
  try {
    hz_cache_.putBody(variant_hash_key_, body_order_++, bodyEntry);
  } catch (HazelcastClientOfflineException e) {
    ENVOY_LOG(warn, "Hazelcast cluster connection is lost!");
  }
  if (body_order_ == ConfigUtil::partitionWarnLimit()) {
    ENVOY_LOG(warn, "Number of body partitions for a response has been reached {} (or more).",
        ConfigUtil::partitionWarnLimit());
    ENVOY_LOG(info, "Having so many partitions might cause performance drop "
                    "as well as extra memory usage. Consider increasing body"
                    "partition size.");
  }
}

void DividedInsertContext::flushHeader() {
  ASSERT(!abort_insertion_);
  ASSERT(!committed_end_stream_);
  committed_end_stream_ = true;
  HazelcastHeaderEntry header(std::move(header_map_), std::move(variant_key_),
      total_body_size_, version_);
  try {
    hz_cache_.putHeader(variant_hash_key_, header);
    hz_cache_.unlock(variant_hash_key_);
    ENVOY_LOG(debug, "Inserted header entry with key {}u", variant_hash_key_);
  } catch (HazelcastClientOfflineException e) {
    ENVOY_LOG(warn, "Hazelcast Connection is offline!");
    // To handle leftover locks, hazelcast.lock.max.lease.time.seconds property must
    // be set to a reasonable value on the server side. It is Long.MAX by default.
    // To make this independent from the server configuration, tryLock with leaseTime
    // option can be used when available in a future release of cpp client. The related
    // issue can be tracked at: https://github.com/hazelcast/hazelcast-cpp-client/issues/579
    // TODO(enozcan): Use tryLock with leaseTime when released for Hazelcast cpp client.
  }
}

} // HazelcastHttpCache
} // Cache
} // HttpFilters
} // Extensions
} // Envoy
