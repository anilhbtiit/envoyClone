#include "common/buffer/buffer_impl.h"

#include <bits/stdint-uintn.h>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "common/common/assert.h"

#include "absl/container/fixed_array.h"
#include "envoy/buffer/buffer.h"
#include "event2/buffer.h"

namespace Envoy {
namespace Buffer {
namespace {
// This size has been determined to be optimal from running the
// //test/integration:http_benchmark benchmark tests.
// TODO(yanavlasov): This may not be optimal for all hardware configurations or traffic patterns and
// may need to be configurable in the future.
constexpr uint64_t CopyThreshold = 512;
} // namespace

thread_local absl::InlinedVector<Slice::StoragePtr, Slice::free_list_max_> Slice::free_list_;

void OwnedImpl::addImpl(const void* data, uint64_t size) {
  const char* src = static_cast<const char*>(data);
  bool new_slice_needed = slices_.empty();
  while (size != 0) {
    if (new_slice_needed) {
      slices_.emplace_back(Slice(size, account_));
    }
    uint64_t copy_size = slices_.back().append(src, size);
    src += copy_size;
    size -= copy_size;
    length_ += copy_size;
    new_slice_needed = true;
  }
}

void OwnedImpl::addDrainTracker(std::function<void()> drain_tracker) {
  ASSERT(!slices_.empty());
  slices_.back().addDrainTracker(std::move(drain_tracker));
}

void OwnedImpl::bindAccount(BufferMemoryAccountSharedPtr account) {
  ASSERT(slices_.empty());
  // We don't yet have an account bound.
  ASSERT(!account_);
  account_ = std::move(account);
}

void OwnedImpl::add(const void* data, uint64_t size) { addImpl(data, size); }

void OwnedImpl::addBufferFragment(BufferFragment& fragment) {
  length_ += fragment.size();
  slices_.emplace_back(fragment);
}

std::pair<bool, size_t> findDequeIndex(size_t i, RawSlice& slice, SliceDeque& slices) {
  size_t empty_slices = 0;
  size_t deque_index = 0;

  // This is the same logic used by getRawSlices require any changes upstream applied here too
  for (const auto& deque_slice : slices) {
    if (deque_slice.dataSize() == 0) {
      ++empty_slices;
      continue;
    }
    if (deque_index == i && deque_slice.data() == slice.mem_ &&
        deque_slice.dataSize() == slice.len_) {
      return {true, deque_index + empty_slices};
    }
    ++deque_index;
  }

  return {false, deque_index + empty_slices};
}

void OwnedImpl::add(absl::string_view data) { add(data.data(), data.size()); }

void OwnedImpl::add(const Instance& data) {
  ASSERT(&data != this);
  for (const RawSlice& slice : data.getRawSlices()) {
    add(slice.mem_, slice.len_);
  }
}

void OwnedImpl::prepend(absl::string_view data) {
  uint64_t size = data.size();
  bool new_slice_needed = slices_.empty();
  while (size != 0) {
    if (new_slice_needed) {
      slices_.emplace_front(Slice(size, account_));
    }
    uint64_t copy_size = slices_.front().prepend(data.data(), size);
    size -= copy_size;
    length_ += copy_size;
    new_slice_needed = true;
  }
}

void OwnedImpl::prepend(Instance& data) {
  ASSERT(&data != this);
  OwnedImpl& other = static_cast<OwnedImpl&>(data);
  while (!other.slices_.empty()) {
    uint64_t slice_size = other.slices_.back().dataSize();
    length_ += slice_size;
    slices_.emplace_front(std::move(other.slices_.back()));
    slices_.front().maybeChargeAccount(account_);
    other.slices_.pop_back();
    other.length_ -= slice_size;
  }
  other.postProcess();
}

void OwnedImpl::copyOut(size_t start, uint64_t size, void* data) const {
  uint64_t bytes_to_skip = start;
  uint8_t* dest = static_cast<uint8_t*>(data);
  for (const auto& slice : slices_) {
    if (size == 0) {
      break;
    }
    uint64_t data_size = slice.dataSize();
    if (data_size <= bytes_to_skip) {
      // The offset where the caller wants to start copying is after the end of this slice,
      // so just skip over this slice completely.
      bytes_to_skip -= data_size;
      continue;
    }
    uint64_t copy_size = std::min(size, data_size - bytes_to_skip);
    memcpy(dest, slice.data() + bytes_to_skip, copy_size); // NOLINT(safe-memcpy)
    size -= copy_size;
    dest += copy_size;
    // Now that we've started copying, there are no bytes left to skip over. If there
    // is any more data to be copied, the next iteration can start copying from the very
    // beginning of the next slice.
    bytes_to_skip = 0;
  }
  ASSERT(size == 0);
}

void OwnedImpl::drain(uint64_t size) { drainImpl(size); }

void OwnedImpl::drainImpl(uint64_t size) {
  while (size != 0) {
    if (slices_.empty()) {
      break;
    }
    uint64_t slice_size = slices_.front().dataSize();
    if (slice_size <= size) {
      slices_.pop_front();
      length_ -= slice_size;
      size -= slice_size;
    } else {
      slices_.front().drain(size);
      length_ -= size;
      size = 0;
    }
  }
  // Make sure to drain any zero byte fragments that might have been added as
  // sentinels for flushed data.
  while (!slices_.empty() && slices_.front().dataSize() == 0) {
    slices_.pop_front();
  }
}

void OwnedImpl::drainAtEnd(uint64_t size) {
  while (size != 0) {
    if (slices_.empty()) {
      break;
    }
    uint64_t slice_size = slices_.back().dataSize();
    if (slice_size <= size) {
      slices_.pop_back();
      length_ -= slice_size;
      size -= slice_size;
    } else {
      slices_.back().drainAtEnd(size);
      length_ -= size;
      size = 0;
    }
  }
}

RawSliceVector OwnedImpl::getRawSlices(absl::optional<uint64_t> max_slices) const {
  uint64_t max_out = slices_.size();
  if (max_slices.has_value()) {
    max_out = std::min(max_out, max_slices.value());
  }

  RawSliceVector raw_slices;
  raw_slices.reserve(max_out);
  for (const auto& slice : slices_) {
    if (raw_slices.size() >= max_out) {
      break;
    }

    if (slice.dataSize() == 0) {
      continue;
    }

    // Temporary cast to fix 32-bit Envoy mobile builds, where sizeof(uint64_t) != sizeof(size_t).
    // dataSize represents the size of a buffer so size_t should always be large enough to hold its
    // size regardless of architecture. Buffer slices should in practice be relatively small, but
    // there is currently no max size validation.
    // TODO(antoniovicente) Set realistic limits on the max size of BufferSlice and consider use of
    // size_t instead of uint64_t in the Slice interface.
    raw_slices.emplace_back(
        RawSlice{const_cast<uint8_t*>(slice.data()), static_cast<size_t>(slice.dataSize())});
  }
  return raw_slices;
}

RawSlice OwnedImpl::frontSlice() const {
  // Ignore zero-size slices and return the first slice with data.
  for (const auto& slice : slices_) {
    if (slice.dataSize() > 0) {
      return RawSlice{const_cast<uint8_t*>(slice.data()),
                      static_cast<absl::Span<uint8_t>::size_type>(slice.dataSize())};
    }
  }

  return {nullptr, 0};
}

SliceDataPtr OwnedImpl::extractMutableFrontSlice() {
  RELEASE_ASSERT(length_ > 0, "Extract called on empty buffer");
  // Remove zero byte fragments from the front of the queue to ensure
  // that the extracted slice has data.
  while (!slices_.empty() && slices_.front().dataSize() == 0) {
    slices_.pop_front();
  }
  ASSERT(!slices_.empty());
  auto slice = std::move(slices_.front());
  auto size = slice.dataSize();
  length_ -= size;
  slices_.pop_front();
  if (!slice.isMutable()) {
    // Create a mutable copy of the immutable slice data.
    Slice mutable_slice{size, nullptr};
    auto copy_size = mutable_slice.append(slice.data(), size);
    ASSERT(copy_size == size);
    // Drain trackers for the immutable slice will be called as part of the slice destructor.
    return std::make_unique<SliceDataImpl>(std::move(mutable_slice));
  } else {
    // Make sure drain trackers are called before ownership of the slice is transferred from
    // the buffer to the caller.
    slice.callAndClearDrainTrackersAndCharges();
    return std::make_unique<SliceDataImpl>(std::move(slice));
  }
}

uint64_t OwnedImpl::length() const {
#ifndef NDEBUG
  // When running in debug mode, verify that the precomputed length matches the sum
  // of the lengths of the slices.
  uint64_t length = 0;
  for (const auto& slice : slices_) {
    length += slice.dataSize();
  }
  ASSERT(length == length_);
#endif

  return length_;
}

void* OwnedImpl::linearize(uint32_t size) {
  RELEASE_ASSERT(size <= length(), "Linearize size exceeds buffer size");
  if (slices_.empty()) {
    return nullptr;
  }
  if (slices_[0].dataSize() < size) {
    Slice new_slice{size, account_};
    Slice::Reservation reservation = new_slice.reserve(size);
    ASSERT(reservation.mem_ != nullptr);
    ASSERT(reservation.len_ == size);
    copyOut(0, size, reservation.mem_);
    new_slice.commit(reservation);

    // Replace the first 'size' bytes in the buffer with the new slice. Since new_slice re-adds the
    // drained bytes, avoid use of the overridable 'drain' method to avoid incorrectly checking if
    // we dipped below low-watermark.
    drainImpl(size);
    slices_.emplace_front(std::move(new_slice));
    length_ += size;
  }
  return slices_.front().data();
}

void OwnedImpl::coalesceOrAddSlice(Slice&& other_slice) {
  const uint64_t slice_size = other_slice.dataSize();
  // The `other_slice` content can be coalesced into the existing slice IFF:
  // 1. The `other_slice` can be coalesced. Immutable slices can not be safely coalesced because
  // their destructors can be arbitrary global side effects.
  // 2. There are existing slices;
  // 3. The `other_slice` content length is under the CopyThreshold;
  // 4. There is enough unused space in the existing slice to accommodate the `other_slice` content.
  if (other_slice.canCoalesce() && !slices_.empty() && slice_size < CopyThreshold &&
      slices_.back().reservableSize() >= slice_size) {
    // Copy content of the `other_slice`. The `move` methods which call this method effectively
    // drain the source buffer.
    addImpl(other_slice.data(), slice_size);
    other_slice.transferDrainTrackersTo(slices_.back());
  } else {
    // Take ownership of the slice.
    other_slice.maybeChargeAccount(account_);
    slices_.emplace_back(std::move(other_slice));
    length_ += slice_size;
  }
}

void OwnedImpl::move(Instance& rhs) {
  ASSERT(&rhs != this);
  // We do the static cast here because in practice we only have one buffer implementation right
  // now and this is safe. This is a reasonable compromise in a high performance path where we
  // want to maintain an abstraction.
  OwnedImpl& other = static_cast<OwnedImpl&>(rhs);
  while (!other.slices_.empty()) {
    const uint64_t slice_size = other.slices_.front().dataSize();
    coalesceOrAddSlice(std::move(other.slices_.front()));
    other.length_ -= slice_size;
    other.slices_.pop_front();
  }
  other.postProcess();
}

void OwnedImpl::move(Instance& rhs, uint64_t length) {
  ASSERT(&rhs != this);
  // See move() above for why we do the static cast.
  OwnedImpl& other = static_cast<OwnedImpl&>(rhs);
  while (length != 0 && !other.slices_.empty()) {
    const uint64_t slice_size = other.slices_.front().dataSize();
    const uint64_t copy_size = std::min(slice_size, length);
    if (copy_size == 0) {
      other.slices_.pop_front();
    } else if (copy_size < slice_size) {
      // TODO(brian-pane) add reference-counting to allow slices to share their storage
      // and eliminate the copy for this partial-slice case?
      add(other.slices_.front().data(), copy_size);
      other.slices_.front().drain(copy_size);
      other.length_ -= copy_size;
    } else {
      coalesceOrAddSlice(std::move(other.slices_.front()));
      other.slices_.pop_front();
      other.length_ -= slice_size;
    }
    length -= copy_size;
  }
  other.postProcess();
}

Reservation OwnedImpl::reserveForRead() {
  return reserveWithMaxLength(default_read_reservation_size_);
}

Reservation OwnedImpl::reserveWithMaxLength(uint64_t max_length) {
  Reservation reservation = Reservation::bufferImplUseOnlyConstruct(*this);
  if (max_length == 0) {
    return reservation;
  }

  // Remove any empty slices at the end.
  while (!slices_.empty() && slices_.back().dataSize() == 0) {
    slices_.pop_back();
  }

  uint64_t bytes_remaining = max_length;
  uint64_t reserved = 0;
  auto& reservation_slices = reservation.bufferImplUseOnlySlices();
  auto slices_owner = std::make_unique<OwnedImplReservationSlicesOwnerMultiple>();

  // Check whether there are any empty slices with reservable space at the end of the buffer.
  uint64_t reservable_size = slices_.empty() ? 0 : slices_.back().reservableSize();
  if (reservable_size >= max_length || reservable_size >= (Slice::default_slice_size_ / 8)) {
    auto& last_slice = slices_.back();
    const uint64_t reservation_size = std::min(last_slice.reservableSize(), bytes_remaining);
    auto slice = last_slice.reserve(reservation_size);
    reservation_slices.push_back(slice);
    slices_owner->owned_slices_.emplace_back(Slice());
    bytes_remaining -= slice.len_;
    reserved += slice.len_;
  }

  while (bytes_remaining != 0 && reservation_slices.size() < reservation.MAX_SLICES_) {
    const uint64_t size = Slice::default_slice_size_;

    // If the next slice would go over the desired size, and the amount already reserved is already
    // at least one full slice in size, stop allocating slices. This prevents returning a
    // reservation larger than requested, which could go above the watermark limits for a watermark
    // buffer, unless the size would be very small (less than 1 full slice).
    if (size > bytes_remaining && reserved >= size) {
      break;
    }

    // We will tag the reservation slices on commit. This avoids unnecessary
    // work in the case that the entire reservation isn't used.
    Slice slice(size, nullptr, slices_owner->free_list_);
    const auto raw_slice = slice.reserve(size);
    reservation_slices.push_back(raw_slice);
    slices_owner->owned_slices_.emplace_back(std::move(slice));
    bytes_remaining -= std::min<uint64_t>(raw_slice.len_, bytes_remaining);
    reserved += raw_slice.len_;
  }

  ASSERT(reservation_slices.size() == slices_owner->owned_slices_.size());
  reservation.bufferImplUseOnlySlicesOwner() = std::move(slices_owner);
  reservation.bufferImplUseOnlySetLength(reserved);

  return reservation;
}

ReservationSingleSlice OwnedImpl::reserveSingleSlice(uint64_t length, bool separate_slice) {
  ReservationSingleSlice reservation = ReservationSingleSlice::bufferImplUseOnlyConstruct(*this);
  if (length == 0) {
    return reservation;
  }

  // Remove any empty slices at the end.
  while (!slices_.empty() && slices_.back().dataSize() == 0) {
    slices_.pop_back();
  }

  auto& reservation_slice = reservation.bufferImplUseOnlySlice();
  auto slice_owner = std::make_unique<OwnedImplReservationSlicesOwnerSingle>();

  // Check whether there are any empty slices with reservable space at the end of the buffer.
  uint64_t reservable_size =
      (separate_slice || slices_.empty()) ? 0 : slices_.back().reservableSize();
  if (reservable_size >= length) {
    reservation_slice = slices_.back().reserve(length);
  } else {
    Slice slice(length, account_);
    reservation_slice = slice.reserve(length);
    slice_owner->owned_slice_ = std::move(slice);
  }

  reservation.bufferImplUseOnlySliceOwner() = std::move(slice_owner);

  return reservation;
}

void OwnedImpl::commit(uint64_t length, absl::Span<RawSlice> slices,
                       ReservationSlicesOwnerPtr slices_owner_base) {
  if (length == 0) {
    return;
  }

  ASSERT(dynamic_cast<OwnedImplReservationSlicesOwner*>(slices_owner_base.get()) != nullptr);
  std::unique_ptr<OwnedImplReservationSlicesOwner> slices_owner(
      static_cast<OwnedImplReservationSlicesOwner*>(slices_owner_base.release()));

  absl::Span<Slice> owned_slices = slices_owner->ownedSlices();
  ASSERT(slices.size() == owned_slices.size());

  uint64_t bytes_remaining = length;
  for (uint32_t i = 0; i < slices.size() && bytes_remaining > 0; i++) {
    Slice& owned_slice = owned_slices[i];
    if (owned_slice.data() != nullptr) {
      owned_slice.maybeChargeAccount(account_);
      slices_.emplace_back(std::move(owned_slice));
    }
    slices[i].len_ = std::min<uint64_t>(slices[i].len_, bytes_remaining);
    bool success = slices_.back().commit(slices[i]);
    ASSERT(success);
    length_ += slices[i].len_;
    bytes_remaining -= slices[i].len_;
  }
}

const uint8_t* memchr(const uint8_t* data, const uint8_t ch, size_t count, Equals equals) {
  if (equals == nullptr) {
    return static_cast<const uint8_t*>(std::memchr(data, ch, count));
  }

  if (data == nullptr || count == 0) {
    return nullptr;
  }

  for (size_t i = 0; i < count; i++) {
    if (equals(data[i], ch)) {
      return data + i;
    }
  }
  return nullptr;
}

ssize_t OwnedImpl::search(const void* data, uint64_t size, size_t start, size_t length) const {
  if (size == 0 && start <= length_) {
    return start;
  }

  return find(data, size, start, length, false, nullptr).offset_.value();
}

IteratorPtr OwnedImpl::search(const void* data, uint64_t size, size_t start, size_t length,
                              bool partial_match_at_end, Equals equals) {

  if (size == 0) {
    if (start == length_) {
      return end();
    }

    auto itr = begin();
    auto itrend = end();
    for (uint64_t i = 0; i < start && (*itr) != (*itrend); i++) {
      ++(*itr);
    }
    return itr;
  }

  return std::unique_ptr<OwnedImpl::OwnendImplIterator>{new OwnedImpl::OwnendImplIterator{
      *this, find(data, size, start, length, partial_match_at_end, equals)}};
}

OwnedImpl::OwnendImplIterator::Location OwnedImpl::find(const void* data, uint64_t size,
                                                        size_t start, size_t length,
                                                        bool partial_match_at_end,
                                                        Equals equals) const {
  // This implementation uses the same search algorithm as evbuffer_search(), a naive
  // scan that requires O(M*N) comparisons in the worst case.
  // TODO(brian-pane): replace this with a more efficient search if it shows up
  // prominently in CPU profiling.
  if (size == 0 && start > length_) {
    return OwnedImpl::OwnendImplIterator::Location{slices_.size(), 0, -1};
  }

  // length equal to zero means that entire buffer must be searched.
  // Adjust the length to buffer length taking the staring index into account.
  size_t left_to_search = length;
  if (0 == length) {
    left_to_search = length_ - start;
  }
  ssize_t offset = 0;
  const uint8_t* needle = static_cast<const uint8_t*>(data);
  for (size_t slice_index = 0; slice_index < slices_.size() && (left_to_search > 0);
       slice_index++) {
    const auto& slice = slices_[slice_index];
    uint64_t slice_size = slice.dataSize();
    if (slice_size <= start) {
      start -= slice_size;
      offset += slice_size;
      continue;
    }
    const uint8_t* slice_start = slice.data();
    const uint8_t* haystack = slice_start;
    const uint8_t* haystack_end = haystack + slice_size;
    haystack += start;
    while (haystack < haystack_end) {
      const size_t slice_search_limit =
          std::min(static_cast<size_t>(haystack_end - haystack), left_to_search);
      // Search within this slice for the first byte of the needle.
      const uint8_t* first_byte_match =
          static_cast<const uint8_t*>(memchr(haystack, needle[0], slice_search_limit, equals));
      if (first_byte_match == nullptr) {
        left_to_search -= slice_search_limit;
        break;
      }
      // After finding a match for the first byte of the needle, check whether the following
      // bytes in the buffer match the remainder of the needle. Note that the match can span
      // two or more slices.
      left_to_search -= static_cast<size_t>(first_byte_match - haystack + 1);
      // Save the current number of bytes left to search.
      // If the pattern is not found, the search will resume from the next byte
      // and left_to_search value must be restored.
      const size_t saved_left_to_search = left_to_search;
      size_t i = 1;
      bool match = true;
      size_t match_index = slice_index;
      const uint8_t* match_next = first_byte_match + 1;
      const uint8_t* match_end = haystack_end;
      while ((i < size) && (0 < left_to_search)) {
        if (match_next >= match_end) {
          // We've hit the end of this slice, so continue checking against the next slice.
          match_index++;
          if (match_index == slices_.size()) {
            // We've hit the end of the entire buffer.
            break;
          }
          const auto& match_slice = slices_[match_index];
          match_next = match_slice.data();
          match_end = match_next + match_slice.dataSize();
          continue;
        }
        left_to_search--;
        match = (equals == nullptr ? *match_next++ == needle[i] : equals(*match_next++, needle[i]));
        if (!match) {
          break;
        }
        i++;
      }
      if (i == size) {
        // Successful match of the entire needle.
        return OwnedImpl::OwnendImplIterator::Location{
            slice_index, static_cast<uint64_t>(first_byte_match - slice_start),
            offset + (first_byte_match - slice_start)};
      }

      if (partial_match_at_end && match && left_to_search == 0) {
        // Successful partial match at end
        return OwnedImpl::OwnendImplIterator::Location{
            slice_index, static_cast<uint64_t>(first_byte_match - slice_start),
            offset + (first_byte_match - slice_start)};
      }
      // If this wasn't a successful match, start scanning again at the next byte.
      haystack = first_byte_match + 1;
      left_to_search = saved_left_to_search;
    }
    start = 0;
    offset += slice_size;
  }

  return OwnedImpl::OwnendImplIterator::Location{slices_.size(), 0, -1};
}

IteratorPtr OwnedImpl::replace(IteratorPtr index, uint64_t count, const void* data, uint64_t size) {
  if (index == nullptr ){
    return nullptr;
  }

  auto& itr = dynamic_cast<OwnedImpl::OwnendImplIterator&>(*index);

  if ( &itr.owned_impl_ != this || itr.slice_index_ < 0 ||
      itr.slice_index_ >= slices_.size() || itr.slice_offset_ < 0 ||
      itr.slice_offset_ >= slices_[itr.slice_index_].dataSize()) {
    return nullptr;
  }

  // Validate count doesn't exceed sum of data size from index
  auto end_slice_index = itr.slice_index_;
  auto end_slice_offset = itr.slice_offset_;
  auto data_size = slices_[itr.slice_index_].dataSize() - itr.slice_offset_;

  if (data_size > count) {
    end_slice_offset = itr.slice_index_ + count;
  } else {
    for (auto i=itr.slice_index_ + 1; i < slices_.size(); i++) {
      end_slice_index = i;
      data_size += slices_[i].dataSize();
      if (data_size > count) {
        end_slice_offset = slices_[i].dataSize() - (data_size - count);
        break;
      }
    }
    count = std::min(count, data_size);
  }

  if (count == 0 && size == 0) {
    return nullptr;
  }

  size_t copied_size = 0;
  auto last_slice_index = itr.slice_index_;
  auto last_slice_offset = itr.slice_offset_;
  const uint8_t* replacer = static_cast<const uint8_t*>(data);

  // First copy min(count, size) lengh bytes of data into buffer
  for (size_t i=itr.slice_index_; i <= end_slice_index && size > 0; i++){
    uint64_t copy_size = 0;
    auto& slice = slices_[i];

    last_slice_index = i;
    if (i == itr.slice_index_) {
      auto drain_size =  std::min(count, slice.dataSize() - itr.slice_offset_);
      if (itr.slice_offset_ == 0) {
        // Replace everthing/prefix
        slice.drain(drain_size);
        last_slice_offset = copy_size = slice.prepend(replacer + copied_size, size);
      } else if (itr.slice_offset_ + drain_size ==  slice.dataSize()) {
        // Replace suffix
        slice.drainAtEnd(drain_size);
        copy_size = slice.append(replacer + copied_size, size);
        last_slice_offset = itr.slice_offset_ + copy_size;
      } else {
        // Replace middle
        memcpy(slice.data() + itr.slice_offset_, replacer + copied_size, drain_size);
        last_slice_offset =  itr.slice_offset_ + copy_size;
        copy_size = drain_size;
      }
    } else if (i == end_slice_index) {
      slice.drain(end_slice_offset);
      last_slice_offset = copy_size = slice.prepend(replacer + copied_size, size);
    } else {
      slice.drain(slice.dataSize());
      last_slice_offset = copy_size = slice.append(replacer + copied_size, size);
    }

    size -= copy_size;
    count -= copy_size;
    copied_size += copy_size;
  }

  // handle remaining bytes in replacer
  if (size > 0) {
    Slice* new_slice = nullptr;
    auto& slice = slices_[last_slice_index];
    auto move_size = last_slice_offset < slice.dataSize() ? slice.dataSize() - last_slice_offset : 0;

    // try reserving size bytes
    auto reservation =  slice.reserve(size);
    if (reservation.mem_) {
      slice.commit(reservation);
    }

    auto avail_size = slice.dataSize() - last_slice_offset;
    auto alloc_size = (avail_size - move_size) < size ? size - (avail_size - move_size) : 0;

    if (alloc_size > 0) {
        Slice nslice{alloc_size, nullptr}; //TODO: Accounting
        slices_.emplace_insert(last_slice_index + 1, std::move(nslice)); // check handles empty or append
        new_slice = &slices_[last_slice_index + 1];
    }

    // copy part of the replacer that cannot fit, into new slice 
    if (size > avail_size) {
        assert(new_slice);
        new_slice->append(replacer + copied_size + avail_size, size-avail_size);
    }

    if (move_size > 0) {
      // move the part of slice that cannot fit
      if (new_slice) {
        auto move_offset = avail_size < size ? last_slice_offset : last_slice_offset + size;
        new_slice->append(slice.data() + move_offset,  avail_size < size ? move_size : move_size - size);      
      } 

      // move the part of slice that can fit
      if (avail_size > size) {
        memmove(slice.data() + last_slice_offset + size, slice.data() + last_slice_offset, move_size - size);
      }
    }

    if (avail_size > 0) {
      // copy remaining replacer into slice
      memcpy(slice.data() + last_slice_offset, replacer + copied_size, avail_size < size ? avail_size : size);
    }

    if (avail_size < size){
      return std::unique_ptr<OwnedImpl::OwnendImplIterator>{new OwnedImpl::OwnendImplIterator{*this, last_slice_index + 1, size - avail_size}};
    }

    return std::unique_ptr<OwnedImpl::OwnendImplIterator>{new OwnedImpl::OwnendImplIterator{*this, last_slice_index, last_slice_offset + size}};
  }

  // Handle case where replacement is shorter or slices had reservable space
  for (auto i = last_slice_index; i <= end_slice_index && count > 0; i++) {
    auto& slice = slices_[i];
    if (i == last_slice_index) {
      auto drain_size = std::min(count, slice.dataSize() - last_slice_offset);
      if (drain_size < slice.dataSize() - last_slice_offset) {
        // move 
        auto move_offset =  last_slice_offset + drain_size;
        memmove(slice.data() + last_slice_offset, slice.data() + move_offset, slice.dataSize() -  move_offset);
        last_slice_offset = slice.dataSize();
      }
      slice.drainAtEnd(drain_size);
      count-=drain_size;
    } else if (i == end_slice_index) {
      slice.drain(end_slice_offset);
      count -= end_slice_offset;
    } else {
      count -= slice.dataSize();
      slice.drain(slice.dataSize());
    }
  }

  return std::unique_ptr<OwnedImpl::OwnendImplIterator>{new OwnedImpl::OwnendImplIterator{*this, last_slice_index, last_slice_offset}};
}

bool OwnedImpl::startsWith(absl::string_view data) const {
  if (length() < data.length()) {
    // Buffer is too short to contain data.
    return false;
  }

  if (data.length() == 0) {
    return true;
  }

  const uint8_t* prefix = reinterpret_cast<const uint8_t*>(data.data());
  size_t size = data.length();
  for (const auto& slice : slices_) {
    uint64_t slice_size = slice.dataSize();
    const uint8_t* slice_start = slice.data();

    if (slice_size >= size) {
      // The remaining size bytes of data are in this slice.
      return memcmp(prefix, slice_start, size) == 0;
    }

    // Slice is smaller than data, see if the prefix matches.
    if (memcmp(prefix, slice_start, slice_size) != 0) {
      return false;
    }

    // Prefix matched. Continue looking at the next slice.
    prefix += slice_size;
    size -= slice_size;
  }

  // Less data in slices than length() reported.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

OwnedImpl::OwnedImpl() = default;

OwnedImpl::OwnedImpl(absl::string_view data) : OwnedImpl() { add(data); }

OwnedImpl::OwnedImpl(const Instance& data) : OwnedImpl() { add(data); }

OwnedImpl::OwnedImpl(const void* data, uint64_t size) : OwnedImpl() { add(data, size); }

OwnedImpl::OwnedImpl(BufferMemoryAccountSharedPtr account) : account_(std::move(account)) {}

std::string OwnedImpl::toString() const {
  std::string output;
  output.reserve(length());
  for (const RawSlice& slice : getRawSlices()) {
    output.append(static_cast<const char*>(slice.mem_), slice.len_);
  }

  return output;
}

void OwnedImpl::postProcess() {}

void OwnedImpl::appendSliceForTest(const void* data, uint64_t size) {
  slices_.emplace_back(Slice(size, account_));
  slices_.back().append(data, size);
  length_ += size;
}

void OwnedImpl::appendSliceForTest(absl::string_view data) {
  appendSliceForTest(data.data(), data.size());
}

std::vector<Slice::SliceRepresentation> OwnedImpl::describeSlicesForTest() const {
  std::vector<Slice::SliceRepresentation> slices;
  for (const auto& slice : slices_) {
    slices.push_back(slice.describeSliceForTest());
  }
  return slices;
}

/**
 * Invariant:
 *    slice_index_ >= first non empty slice or 0 and slice_index_ <= owned_impl_.slices_.size()
 *    slice_offset_ >= first readable offset or 0 and slice_offset_ <=
 * ownned_impl_.slices_[slice_index_].dataSize()
 */

Iterator& OwnedImpl::OwnendImplIterator::operator++() {
  // One past the end of buffer
  if (slice_index_ == owned_impl_.slices_.size()) {
    return *this;
  }

  const auto& slice = owned_impl_.slices_[slice_index_];

  // Successful increment
  if (++slice_offset_ < slice.dataSize()) {
    return *this;
  }

  // Done iterating through current slice, skip empty slices and  move to next
  do {
    slice_index_++;
  } while (slice_index_ < owned_impl_.slices_.size() &&
           owned_impl_.slices_[slice_index_].dataSize() == 0);

  slice_offset_ = 0;
  return *this;
}

/**
 * Invariant:
 *    slice_index_ >= 0 and slice_index_ <= owned_impl_.slices_.size()
 *    slice_offset_ >= 0 and slice_offset_ <= ownned_impl_.slices_[slice_index_].dataSize()
 */
Iterator& OwnedImpl::OwnendImplIterator::operator--() {
  // At the begining of buffer
  if (slice_index_ == 0 && slice_offset_ == 0) {
    return *this;
  }

  if (slice_offset_ > 0) { // Guard against underflow
    slice_offset_--;
    return *this;
  }

  // Successful decrement
  // Skip empty slices
  do {
    slice_index_--;
  } while (slice_index_ > 0 && owned_impl_.slices_[slice_index_].dataSize() == 0);

  slice_offset_ = owned_impl_.slices_[slice_index_].dataSize() > 0
                      ? owned_impl_.slices_[slice_index_].dataSize() - 1
                      : 0;
  return *this;
}

bool OwnedImpl::OwnendImplIterator::operator==(const Iterator& rhs) {
  const OwnedImpl::OwnendImplIterator* rhs_owned_impl_itr =
      dynamic_cast<const OwnedImpl::OwnendImplIterator*>(&rhs);

  return rhs_owned_impl_itr != nullptr && &owned_impl_ == &(rhs_owned_impl_itr->owned_impl_) &&
         slice_index_ == rhs_owned_impl_itr->slice_index_ &&
         slice_offset_ == rhs_owned_impl_itr->slice_offset_;
}

bool OwnedImpl::OwnendImplIterator::operator!=(const Iterator& rhs) { return !(*this == rhs); }

uint8_t& OwnedImpl::OwnendImplIterator::operator*() {
  return *(owned_impl_.slices_[slice_index_].data() + slice_offset_);
}

} // namespace Buffer
} // namespace Envoy