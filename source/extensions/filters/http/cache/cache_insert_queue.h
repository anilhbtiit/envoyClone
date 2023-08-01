#pragma once

#include <deque>
#include <functional>

#include "source/extensions/filters/http/cache/http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

using OverHighWatermarkCallback = std::function<void()>;
using UnderLowWatermarkCallback = std::function<void()>;
using AbortInsertCallback = std::function<void()>;
class CacheInsertChunk;

class CacheInsertQueue {
public:
  CacheInsertQueue(Http::StreamEncoderFilterCallbacks& encoder_callbacks,
                   InsertContextPtr insert_context, AbortInsertCallback abort);
  void insertHeaders(const Http::ResponseHeaderMap& response_headers,
                     const ResponseMetadata& metadata, bool end_stream);
  void insertBody(const Buffer::Instance& chunk, bool end_stream);
  void insertTrailers(const Http::ResponseTrailerMap& trailers);
  void takeOwnershipOfYourself(std::unique_ptr<CacheInsertQueue> self);
  ~CacheInsertQueue();

private:
  void onChunkComplete(bool ready_for_next_chunk, bool end_stream, size_t sz);

  Event::Dispatcher& dispatcher_;
  const InsertContextPtr insert_context_;
  const size_t low_watermark_bytes_, high_watermark_bytes_;
  OptRef<Http::StreamEncoderFilterCallbacks> encoder_callbacks_;
  AbortInsertCallback abort_callback_;
  std::deque<std::unique_ptr<CacheInsertChunk>> chunks_;
  size_t queue_size_bytes_ = 0;
  bool watermarked_ = false;
  bool chunk_in_flight_ = false;
  // True if end_stream has been queued. If the queue gets handed ownership
  // of itself before the end is in sight then it might as well abort since
  // it's not going to get a complete entry.
  bool end_stream_queued_ = false;
  // If the filter was deleted while !end_in_sight, aborting_ is set to true;
  // when the next chunk completes (or cancels), the queue is destroyed.
  bool aborting_ = false;
  // When the filter is destroyed, it passes ownership of CacheInsertQueue
  // to itself, because CacheInsertQueue can outlive the filter. The queue
  // will remove its self-ownership (thereby deleting itself) upon
  // completion of its work.
  std::unique_ptr<CacheInsertQueue> self_ownership_;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
