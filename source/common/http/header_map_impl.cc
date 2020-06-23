#include "common/http/header_map_impl.h"

#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/http/header_map.h"

#include "common/common/assert.h"
#include "common/common/dump_state_utils.h"
#include "common/common/empty_string.h"
#include "common/singleton/const_singleton.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Http {

namespace {
// This includes the NULL (StringUtil::itoa technically only needs 21).
constexpr size_t MaxIntegerLength{32};

void validateCapacity(uint64_t new_capacity) {
  // If the resizing will cause buffer overflow due to hitting uint32_t::max, an OOM is likely
  // imminent. Fast-fail rather than allow a buffer overflow attack (issue #1421)
  RELEASE_ASSERT(new_capacity <= std::numeric_limits<uint32_t>::max(),
                 "Trying to allocate overly large headers.");
}

absl::string_view getStrView(const VariantHeader& buffer) {
  return absl::get<absl::string_view>(buffer);
}

InlineHeaderVector& getInVec(VariantHeader& buffer) {
  return absl::get<InlineHeaderVector>(buffer);
}

const InlineHeaderVector& getInVec(const VariantHeader& buffer) {
  return absl::get<InlineHeaderVector>(buffer);
}
} // namespace

// Initialize as a Type::Inline
HeaderString::HeaderString() : buffer_(InlineHeaderVector()) {
  ASSERT((getInVec(buffer_).capacity()) >= MaxIntegerLength);
  ASSERT(valid());
}

// Initialize as a Type::Reference
HeaderString::HeaderString(const LowerCaseString& ref_value)
    : buffer_(absl::string_view(ref_value.get().c_str(), ref_value.get().size())) {
  ASSERT(valid());
}

// Initialize as a Type::Reference
HeaderString::HeaderString(absl::string_view ref_value) : buffer_(ref_value) { ASSERT(valid()); }

HeaderString::HeaderString(HeaderString&& move_value) noexcept
    : buffer_(std::move(move_value.buffer_)) {
  move_value.clear();
  ASSERT(valid());
}

bool HeaderString::valid() const { return validHeaderString(getStringView()); }

void HeaderString::append(const char* data, uint32_t data_size) {
  // Make sure the requested memory allocation is below uint32_t::max
  const uint64_t new_capacity = static_cast<uint64_t>(data_size) + size();
  validateCapacity(new_capacity);
  ASSERT(validHeaderString(absl::string_view(data, data_size)));

  switch (type()) {
  case Type::Reference: {
    // Rather than be too clever and optimize this uncommon case, we switch to
    // Inline mode and copy.
    const absl::string_view prev = getStrView(buffer_);
    buffer_ = InlineHeaderVector();
    // Assigning new_capacity to avoid resizing when appending the new data
    getInVec(buffer_).reserve(new_capacity);
    getInVec(buffer_).assign(prev.begin(), prev.end());
    break;
  }
  case Type::Inline: {
    getInVec(buffer_).reserve(new_capacity);
    break;
  }
  }
  getInVec(buffer_).insert(getInVec(buffer_).end(), data, data + data_size);
}

void HeaderString::rtrim() {
  ASSERT(type() == Type::Inline);
  absl::string_view original = getStringView();
  absl::string_view rtrimmed = StringUtil::rtrim(original);
  if (original.size() != rtrimmed.size()) {
    getInVec(buffer_).resize(rtrimmed.size());
  }
}

absl::string_view HeaderString::getStringView() const {
  if (type() == Type::Reference) {
    return getStrView(buffer_);
  }
  ASSERT(type() == Type::Inline);
  return {getInVec(buffer_).data(), getInVec(buffer_).size()};
}

void HeaderString::clear() {
  if (type() == Type::Inline) {
    getInVec(buffer_).clear();
  }
}

void HeaderString::setCopy(const char* data, uint32_t size) {
  ASSERT(validHeaderString(absl::string_view(data, size)));

  if (!absl::holds_alternative<InlineHeaderVector>(buffer_)) {
    // Switching from Type::Reference to Type::Inline
    buffer_ = InlineHeaderVector();
  }

  getInVec(buffer_).reserve(size);
  getInVec(buffer_).assign(data, data + size);
  ASSERT(valid());
}

void HeaderString::setCopy(absl::string_view view) {
  this->setCopy(view.data(), static_cast<uint32_t>(view.size()));
}

void HeaderString::setInteger(uint64_t value) {
  // Initialize the size to the max length, copy the actual data, and then
  // reduce the size (but not the capacity) as needed
  // Note: instead of using the inner_buffer, attempted the following:
  // resize buffer_ to MaxIntegerLength, apply StringUtil::itoa to the buffer_.data(), and then
  // resize buffer_ to int_length (the number of digits in value).
  // However it was slower than the following approach.
  char inner_buffer[MaxIntegerLength];
  const uint32_t int_length = StringUtil::itoa(inner_buffer, MaxIntegerLength, value);

  if (type() == Type::Reference) {
    // Switching from Type::Reference to Type::Inline
    buffer_ = InlineHeaderVector();
  }
  ASSERT((getInVec(buffer_).capacity()) > MaxIntegerLength);
  getInVec(buffer_).assign(inner_buffer, inner_buffer + int_length);
}

void HeaderString::setReference(absl::string_view ref_value) {
  buffer_ = ref_value;
  ASSERT(valid());
}

uint32_t HeaderString::size() const {
  if (type() == Type::Reference) {
    return getStrView(buffer_).size();
  }
  ASSERT(type() == Type::Inline);
  return getInVec(buffer_).size();
}

HeaderString::Type HeaderString::type() const {
  // buffer_.index() is correlated with the order of Reference and Inline in the
  // enum.
  ASSERT(buffer_.index() == 0 || buffer_.index() == 1);
  ASSERT((buffer_.index() == 0 && absl::holds_alternative<absl::string_view>(buffer_)) ||
         (buffer_.index() != 0));
  ASSERT((buffer_.index() == 1 && absl::holds_alternative<InlineHeaderVector>(buffer_)) ||
         (buffer_.index() != 1));
  return Type(buffer_.index());
}

#if HEADER_MAP_TYPE == HEADER_MAP_TYPE_ORIGINAL

// Specialization needed for HeaderMapImpl::HeaderList::insert() when key is LowerCaseString.
// A fully specialized template must be defined once in the program, hence this may not be in
// a header file.
template <> bool HeaderMapImpl::HeaderList::isPseudoHeader(const LowerCaseString& key) {
  return key.get().c_str()[0] == ':';
}

HeaderMapImpl::HeaderEntryImpl::HeaderEntryImpl(const LowerCaseString& key) : key_(key) {}

HeaderMapImpl::HeaderEntryImpl::HeaderEntryImpl(const LowerCaseString& key, HeaderString&& value)
    : key_(key), value_(std::move(value)) {}

HeaderMapImpl::HeaderEntryImpl::HeaderEntryImpl(HeaderString&& key, HeaderString&& value)
    : key_(std::move(key)), value_(std::move(value)) {}

void HeaderMapImpl::HeaderEntryImpl::value(absl::string_view value) { value_.setCopy(value); }

void HeaderMapImpl::HeaderEntryImpl::value(uint64_t value) { value_.setInteger(value); }

void HeaderMapImpl::HeaderEntryImpl::value(const HeaderEntry& header) {
  value(header.value().getStringView());
}

template <> HeaderMapImpl::StaticLookupTable<RequestHeaderMap>::StaticLookupTable() {
#define REGISTER_DEFAULT_REQUEST_HEADER(name)                                                      \
  CustomInlineHeaderRegistry::registerInlineHeader<RequestHeaderMap::header_map_type>(             \
      Headers::get().name);
  INLINE_REQ_HEADERS(REGISTER_DEFAULT_REQUEST_HEADER)
  INLINE_REQ_RESP_HEADERS(REGISTER_DEFAULT_REQUEST_HEADER)

  finalizeTable();

  // Special case where we map a legacy host header to :authority.
  const auto handle =
      CustomInlineHeaderRegistry::getInlineHeader<RequestHeaderMap::header_map_type>(
          Headers::get().Host);
  add(Headers::get().HostLegacy.get().c_str(), [handle](HeaderMapImpl& h) -> StaticLookupResponse {
    return {&h.inlineHeaders()[handle.value().it_->second], &handle.value().it_->first};
  });
}

template <> HeaderMapImpl::StaticLookupTable<RequestTrailerMap>::StaticLookupTable() {
  finalizeTable();
}

template <> HeaderMapImpl::StaticLookupTable<ResponseHeaderMap>::StaticLookupTable() {
#define REGISTER_RESPONSE_HEADER(name)                                                             \
  CustomInlineHeaderRegistry::registerInlineHeader<ResponseHeaderMap::header_map_type>(            \
      Headers::get().name);
  INLINE_RESP_HEADERS(REGISTER_RESPONSE_HEADER)
  INLINE_REQ_RESP_HEADERS(REGISTER_RESPONSE_HEADER)
  INLINE_RESP_HEADERS_TRAILERS(REGISTER_RESPONSE_HEADER)

  finalizeTable();
}

template <> HeaderMapImpl::StaticLookupTable<ResponseTrailerMap>::StaticLookupTable() {
#define REGISTER_RESPONSE_TRAILER(name)                                                            \
  CustomInlineHeaderRegistry::registerInlineHeader<ResponseTrailerMap::header_map_type>(           \
      Headers::get().name);
  INLINE_RESP_HEADERS_TRAILERS(REGISTER_RESPONSE_TRAILER)

  finalizeTable();
}

#endif // HEADER_MAP_TYPE == HEADER_MAP_TYPE_ORIGINAL

uint64_t HeaderMapImpl::appendToHeader(HeaderString& header, absl::string_view data,
                                       absl::string_view delimiter) {
  if (data.empty()) {
    return 0;
  }
  uint64_t byte_size = 0;
  if (!header.empty()) {
    header.append(delimiter.data(), delimiter.size());
    byte_size += delimiter.size();
  }
  header.append(data.data(), data.size());
  return data.size() + byte_size;
}

void HeaderMapImpl::updateSize(uint64_t from_size, uint64_t to_size) {
  ASSERT(cached_byte_size_ >= from_size);
  cached_byte_size_ -= from_size;
  cached_byte_size_ += to_size;
}

void HeaderMapImpl::addSize(uint64_t size) { cached_byte_size_ += size; }

void HeaderMapImpl::subtractSize(uint64_t size) {
  ASSERT(cached_byte_size_ >= size);
  cached_byte_size_ -= size;
}

void HeaderMapImpl::copyFrom(HeaderMap& lhs, const HeaderMap& header_map) {
  header_map.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        // TODO(mattklein123) PERF: Avoid copying here if not necessary.
        HeaderString key_string;
        key_string.setCopy(header.key().getStringView());
        HeaderString value_string;
        value_string.setCopy(header.value().getStringView());

        static_cast<HeaderMap*>(context)->addViaMove(std::move(key_string),
                                                     std::move(value_string));
        return HeaderMap::Iterate::Continue;
      },
      &lhs);
}

namespace {

// This is currently only used in tests and is not optimized for performance.
HeaderMap::Iterate collectAllHeaders(const HeaderEntry& header, void* headers) {
  static_cast<std::vector<std::pair<absl::string_view, absl::string_view>>*>(headers)->push_back(
      std::make_pair(header.key().getStringView(), header.value().getStringView()));
  return HeaderMap::Iterate::Continue;
};

} // namespace

#if HEADER_MAP_TYPE == HEADER_MAP_TYPE_ORIGINAL

// This is currently only used in tests and is not optimized for performance.
bool HeaderMapImpl::operator==(const HeaderMap& rhs) const {
  if (size() != rhs.size()) {
    return false;
  }

  std::vector<std::pair<absl::string_view, absl::string_view>> rhs_headers;
  rhs_headers.reserve(rhs.size());
  rhs.iterate(collectAllHeaders, &rhs_headers);

  auto i = headers_.begin();
  auto j = rhs_headers.begin();
  for (; i != headers_.end(); ++i, ++j) {
    if (i->key() != j->first || i->value() != j->second) {
      return false;
    }
  }

  return true;
}

bool HeaderMapImpl::operator!=(const HeaderMap& rhs) const { return !operator==(rhs); }

void HeaderMapImpl::insertByKey(HeaderString&& key, HeaderString&& value) {
  auto lookup = staticLookup(key.getStringView());
  if (lookup.has_value()) {
    key.clear();
    if (*lookup.value().entry_ == nullptr) {
      maybeCreateInline(lookup.value().entry_, *lookup.value().key_, std::move(value));
    } else {
      const uint64_t added_size =
          appendToHeader((*lookup.value().entry_)->value(), value.getStringView());
      addSize(added_size);
      value.clear();
    }
  } else {
    addSize(key.size() + value.size());
    std::list<HeaderEntryImpl>::iterator i = headers_.insert(std::move(key), std::move(value));
    i->entry_ = i;
  }
}

void HeaderMapImpl::addViaMove(HeaderString&& key, HeaderString&& value) {
  // If this is an inline header, we can't addViaMove, because we'll overwrite
  // the existing value.
  auto* entry = getExistingInline(key.getStringView());
  if (entry != nullptr) {
    const uint64_t added_size = appendToHeader(entry->value(), value.getStringView());
    addSize(added_size);
    key.clear();
    value.clear();
  } else {
    insertByKey(std::move(key), std::move(value));
  }
}

void HeaderMapImpl::addReference(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString ref_value(value);
  addViaMove(std::move(ref_key), std::move(ref_value));
}

void HeaderMapImpl::addReferenceKey(const LowerCaseString& key, uint64_t value) {
  HeaderString ref_key(key);
  HeaderString new_value;
  new_value.setInteger(value);
  insertByKey(std::move(ref_key), std::move(new_value));
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
}

void HeaderMapImpl::addReferenceKey(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString new_value;
  new_value.setCopy(value);
  insertByKey(std::move(ref_key), std::move(new_value));
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
}

void HeaderMapImpl::addCopy(const LowerCaseString& key, uint64_t value) {
  auto* entry = getExistingInline(key.get());
  if (entry != nullptr) {
    char buf[32];
    StringUtil::itoa(buf, sizeof(buf), value);
    const uint64_t added_size = appendToHeader(entry->value(), buf);
    addSize(added_size);
    return;
  }
  HeaderString new_key;
  new_key.setCopy(key.get());
  HeaderString new_value;
  new_value.setInteger(value);
  insertByKey(std::move(new_key), std::move(new_value));
  ASSERT(new_key.empty());   // NOLINT(bugprone-use-after-move)
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
}

void HeaderMapImpl::addCopy(const LowerCaseString& key, absl::string_view value) {
  auto* entry = getExistingInline(key.get());
  if (entry != nullptr) {
    const uint64_t added_size = appendToHeader(entry->value(), value);
    addSize(added_size);
    return;
  }
  HeaderString new_key;
  new_key.setCopy(key.get());
  HeaderString new_value;
  new_value.setCopy(value);
  insertByKey(std::move(new_key), std::move(new_value));
  ASSERT(new_key.empty());   // NOLINT(bugprone-use-after-move)
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
}

void HeaderMapImpl::appendCopy(const LowerCaseString& key, absl::string_view value) {
  // TODO(#9221): converge on and document a policy for coalescing multiple headers.
  auto* entry = getExisting(key);
  if (entry) {
    const uint64_t added_size = appendToHeader(entry->value(), value);
    addSize(added_size);
  } else {
    addCopy(key, value);
  }
}

void HeaderMapImpl::setReference(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString ref_value(value);
  remove(key);
  insertByKey(std::move(ref_key), std::move(ref_value));
}

void HeaderMapImpl::setReferenceKey(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString new_value;
  new_value.setCopy(value);
  remove(key);
  insertByKey(std::move(ref_key), std::move(new_value));
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
}

void HeaderMapImpl::setCopy(const LowerCaseString& key, absl::string_view value) {
  // Replaces the first occurrence of a header if it exists, otherwise adds by copy.
  // TODO(#9221): converge on and document a policy for coalescing multiple headers.
  auto* entry = getExisting(key);
  if (entry) {
    updateSize(entry->value().size(), value.size());
    entry->value(value);
  } else {
    addCopy(key, value);
  }
}

uint64_t HeaderMapImpl::byteSize() const { return cached_byte_size_; }

void HeaderMapImpl::verifyByteSizeInternalForTest() const {
  // Computes the total byte size by summing the byte size of the keys and values.
  uint64_t byte_size = 0;
  for (const HeaderEntryImpl& header : headers_) {
    byte_size += header.key().size();
    byte_size += header.value().size();
  }
  ASSERT(cached_byte_size_ == byte_size);
}

const HeaderEntry* HeaderMapImpl::get(const LowerCaseString& key) const {
  for (const HeaderEntryImpl& header : headers_) {
    if (header.key() == key.get().c_str()) {
      return &header;
    }
  }

  return nullptr;
}

HeaderEntry* HeaderMapImpl::getExisting(const LowerCaseString& key) {
  for (HeaderEntryImpl& header : headers_) {
    if (header.key() == key.get().c_str()) {
      return &header;
    }
  }

  return nullptr;
}

void HeaderMapImpl::iterate(HeaderMap::ConstIterateCb cb, void* context) const {
  for (const HeaderEntryImpl& header : headers_) {
    if (cb(header, context) == HeaderMap::Iterate::Break) {
      break;
    }
  }
}

void HeaderMapImpl::iterateReverse(HeaderMap::ConstIterateCb cb, void* context) const {
  for (auto it = headers_.rbegin(); it != headers_.rend(); it++) {
    if (cb(*it, context) == HeaderMap::Iterate::Break) {
      break;
    }
  }
}

HeaderMap::Lookup HeaderMapImpl::lookup(const LowerCaseString& key,
                                        const HeaderEntry** entry) const {
  // The accessor callbacks for predefined inline headers take a HeaderMapImpl& as an argument;
  // even though we don't make any modifications, we need to const_cast in order to use the
  // accessor.
  //
  // Making this work without const_cast would require managing an additional const accessor
  // callback for each predefined inline header and add to the complexity of the code.
  auto lookup = const_cast<HeaderMapImpl*>(this)->staticLookup(key.get());
  if (lookup.has_value()) {
    *entry = *lookup.value().entry_;
    if (*entry) {
      return HeaderMap::Lookup::Found;
    } else {
      return HeaderMap::Lookup::NotFound;
    }
  } else {
    *entry = nullptr;
    return HeaderMap::Lookup::NotSupported;
  }
}

void HeaderMapImpl::clear() {
  clearInline();
  headers_.clear();
  cached_byte_size_ = 0;
}

size_t HeaderMapImpl::remove(const LowerCaseString& key) {
  const size_t old_size = headers_.size();
  auto lookup = staticLookup(key.get());
  if (lookup.has_value()) {
    removeInline(lookup.value().entry_);
  } else {
    for (auto i = headers_.begin(); i != headers_.end();) {
      if (i->key() == key.get().c_str()) {
        subtractSize(i->key().size() + i->value().size());
        i = headers_.erase(i);
      } else {
        ++i;
      }
    }
  }
  return old_size - headers_.size();
}

size_t HeaderMapImpl::removePrefix(const LowerCaseString& prefix) {
  const size_t old_size = headers_.size();
  headers_.remove_if([&prefix, this](const HeaderEntryImpl& entry) {
    bool to_remove = absl::StartsWith(entry.key().getStringView(), prefix.get());
    if (to_remove) {
      // If this header should be removed, make sure any references in the
      // static lookup table are cleared as well.
      auto lookup = staticLookup(entry.key().getStringView());
      if (lookup.has_value()) {
        if (lookup.value().entry_) {
          const uint32_t key_value_size =
              (*lookup.value().entry_)->key().size() + (*lookup.value().entry_)->value().size();
          subtractSize(key_value_size);
          *lookup.value().entry_ = nullptr;
        }
      } else {
        subtractSize(entry.key().size() + entry.value().size());
      }
    }
    return to_remove;
  });
  return old_size - headers_.size();
}

void HeaderMapImpl::dumpState(std::ostream& os, int indent_level) const {
  using IterateData = std::pair<std::ostream*, const char*>;
  const char* spaces = spacesForLevel(indent_level);
  IterateData iterate_data = std::make_pair(&os, spaces);
  iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        auto* data = static_cast<IterateData*>(context);
        *data->first << data->second << "'" << header.key().getStringView() << "', '"
                     << header.value().getStringView() << "'\n";
        return HeaderMap::Iterate::Continue;
      },
      &iterate_data);
}

HeaderMapImpl::HeaderEntryImpl& HeaderMapImpl::maybeCreateInline(HeaderEntryImpl** entry,
                                                                 const LowerCaseString& key) {
  if (*entry) {
    return **entry;
  }

  addSize(key.get().size());
  std::list<HeaderEntryImpl>::iterator i = headers_.insert(key);
  i->entry_ = i;
  *entry = &(*i);
  return **entry;
}

HeaderMapImpl::HeaderEntryImpl& HeaderMapImpl::maybeCreateInline(HeaderEntryImpl** entry,
                                                                 const LowerCaseString& key,
                                                                 HeaderString&& value) {
  if (*entry) {
    value.clear();
    return **entry;
  }

  addSize(key.get().size() + value.size());
  std::list<HeaderEntryImpl>::iterator i = headers_.insert(key, std::move(value));
  i->entry_ = i;
  *entry = &(*i);
  return **entry;
}

HeaderMapImpl::HeaderEntryImpl* HeaderMapImpl::getExistingInline(absl::string_view key) {
  auto lookup = staticLookup(key);
  if (lookup.has_value()) {
    return *lookup.value().entry_;
  }
  return nullptr;
}

size_t HeaderMapImpl::removeInline(HeaderEntryImpl** ptr_to_entry) {
  if (!*ptr_to_entry) {
    return 0;
  }

  HeaderEntryImpl* entry = *ptr_to_entry;
  const uint64_t size_to_subtract = entry->entry_->key().size() + entry->entry_->value().size();
  subtractSize(size_to_subtract);
  *ptr_to_entry = nullptr;
  headers_.erase(entry->entry_);
  return 1;
}

namespace {
template <class T>
HeaderMapImplUtility::HeaderMapImplInfo makeHeaderMapImplInfo(absl::string_view name) {
  // Constructing a header map implementation will force the custom headers and sizing to be
  // finalized, so do that first.
  auto header_map = T::create();

  HeaderMapImplUtility::HeaderMapImplInfo info;
  info.name_ = std::string(name);
  info.size_ = T::inlineHeadersSize() + sizeof(T);
  for (const auto& header : CustomInlineHeaderRegistry::headers<T::header_map_type>()) {
    info.registered_headers_.push_back(header.first.get());
  }
  return info;
}
} // namespace

std::vector<HeaderMapImplUtility::HeaderMapImplInfo>
HeaderMapImplUtility::getAllHeaderMapImplInfo() {
  std::vector<HeaderMapImplUtility::HeaderMapImplInfo> ret;
  ret.push_back(makeHeaderMapImplInfo<RequestHeaderMapImpl>("request header map"));
  ret.push_back(makeHeaderMapImplInfo<RequestTrailerMapImpl>("request trailer map"));
  ret.push_back(makeHeaderMapImplInfo<ResponseHeaderMapImpl>("response header map"));
  ret.push_back(makeHeaderMapImplInfo<ResponseTrailerMapImpl>("response trailer map"));
  return ret;
}

#elif HEADER_MAP_TYPE == HEADER_MAP_TYPE_FLAT_HASH_MAP

// This is currently only used in tests and is not optimized for performance.
bool HeaderMapImpl::operator==(const HeaderMap& rhs) const {
  if (size() != rhs.size()) {
    return false;
  }

  std::vector<std::pair<absl::string_view, absl::string_view>> lhs_headers;
  lhs_headers.reserve(size());
  iterate(collectAllHeaders, &lhs_headers);

  std::vector<std::pair<absl::string_view, absl::string_view>> rhs_headers;
  rhs_headers.reserve(rhs.size());
  rhs.iterate(collectAllHeaders, &rhs_headers);

  auto i = lhs_headers.begin();
  auto j = rhs_headers.begin();
  for (; i != lhs_headers.end(); ++i, ++j) {
    if (i->first != j->first || i->second != j->second) {
      return false;
    }
  }

  return true;
}

bool HeaderMapImpl::operator!=(const HeaderMap& rhs) const { return !operator==(rhs); }

HeaderMapImpl::HeaderEntryImpl::HeaderEntryImpl(const LowerCaseString& key) : key_(key) {}

HeaderMapImpl::HeaderEntryImpl::HeaderEntryImpl(const LowerCaseString& key, HeaderString&& value,
                                                uint32_t index, bool coalesced)
    : key_(key), value_(std::move(value)), index_(index), coalesced_(coalesced) {}

HeaderMapImpl::HeaderEntryImpl::HeaderEntryImpl(HeaderString&& key, HeaderString&& value,
                                                uint32_t index, bool coalesced)
    : key_(std::move(key)), value_(std::move(value)), index_(index), coalesced_(coalesced) {}

void HeaderMapImpl::HeaderEntryImpl::value(absl::string_view value) { value_.setCopy(value); }

void HeaderMapImpl::HeaderEntryImpl::value(uint64_t value) { value_.setInteger(value); }

void HeaderMapImpl::HeaderEntryImpl::value(const HeaderEntry& header) {
  value(header.value().getStringView());
}

void HeaderMapImpl::addViaMove(HeaderString&& key, HeaderString&& value) {
  const auto& entry = headers_map_.find(key.getStringView());
  if (entry != headers_map_.end()) {
    if (entry->second.front().coalesced_) {
      // A coalesced header, append using delimiter
      const uint64_t added_size =
          appendToHeader(entry->second.front().value(), value.getStringView());
      addSize(added_size);
      key.clear();
      value.clear();
    } else {
      // Add the header to an existing vector
      addSize(key.size() + value.size());
      entry->second.emplace_back(std::move(key), std::move(value), next_header_index_++, false);
      all_headers_num_++;
    }
  } else {
    // Create a new entry
    const bool coalesced = canCoalesce(key);
    addSize(key.size() + value.size());
    std::list<HeaderEntryImpl> header_entry;
    header_entry.emplace_back(std::move(key), std::move(value), next_header_index_++, coalesced);
    headers_map_.emplace(header_entry.front().key().getStringView(), std::move(header_entry));
    all_headers_num_++;
  }
}

void HeaderMapImpl::addReference(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString ref_value(value);
  addViaMove(std::move(ref_key), std::move(ref_value));
}

void HeaderMapImpl::addReferenceKey(const LowerCaseString& key, uint64_t value) {
  HeaderString ref_key(key);
  HeaderString new_value;
  new_value.setInteger(value);
  addViaMove(std::move(ref_key), std::move(new_value));
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
}

void HeaderMapImpl::addReferenceKey(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString new_value;
  new_value.setCopy(value);
  addViaMove(std::move(ref_key), std::move(new_value));
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
}

void HeaderMapImpl::addCopy(const LowerCaseString& key, uint64_t value) {
  HeaderString new_key;
  new_key.setCopy(key.get());
  HeaderString new_value;
  new_value.setInteger(value);
  addViaMove(std::move(new_key), std::move(new_value));
  ASSERT(new_key.empty());   // NOLINT(bugprone-use-after-move)
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
}

void HeaderMapImpl::addCopy(const LowerCaseString& key, absl::string_view value) {
  HeaderString new_key;
  new_key.setCopy(key.get());
  HeaderString new_value;
  new_value.setCopy(value);
  addViaMove(std::move(new_key), std::move(new_value));
  ASSERT(new_key.empty());   // NOLINT(bugprone-use-after-move)
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
}

void HeaderMapImpl::appendCopy(const LowerCaseString& key, absl::string_view value) {
  // TODO(#9221): converge on and document a policy for coalescing multiple headers.
  auto* entry = getExisting(key);
  if (entry) {
    const uint64_t added_size = appendToHeader(entry->value(), value);
    addSize(added_size);
  } else {
    addCopy(key, value);
  }
}

void HeaderMapImpl::setReference(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString ref_value(value);
  remove(key);
  addViaMove(std::move(ref_key), std::move(ref_value));
}

void HeaderMapImpl::setReferenceKey(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString new_value;
  new_value.setCopy(value);
  remove(key);
  addViaMove(std::move(ref_key), std::move(new_value));
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
}

void HeaderMapImpl::setCopy(const LowerCaseString& key, absl::string_view value) {
  // Replaces the first occurrence of a header if it exists, otherwise adds by copy.
  // TODO(#9221): converge on and document a policy for coalescing multiple headers.
  auto* entry = getExisting(key);
  if (entry) {
    updateSize(entry->value().size(), value.size());
    entry->value(value);
  } else {
    addCopy(key, value);
  }
}

uint64_t HeaderMapImpl::byteSize() const { return cached_byte_size_; }

void HeaderMapImpl::verifyByteSizeInternalForTest() const {
  // Computes the total byte size by summing the byte size of the keys and values.
  uint64_t byte_size = 0;
  for (const auto& map_it : headers_map_) {
    for (const HeaderEntryImpl& header : map_it.second) {
      byte_size += header.key().size();
      byte_size += header.value().size();
    }
  }
  ASSERT(cached_byte_size_ == byte_size);
}

const HeaderEntry* HeaderMapImpl::get(const LowerCaseString& key) const {
  // Returns the first occurrence of a Header (if any)
  const HeaderEagerMapValue* value_list = findMapElement(key);
  if (value_list == nullptr) {
    return nullptr;
  }

  return &value_list->front();
}

void HeaderMapImpl::mapToSortedVector(const HeaderEagerMap& map,
                                      std::vector<const HeaderEntryImpl*>& output_vector) const {
  ASSERT(output_vector.empty());
  // Add all HeaderEntryImpl elements, and then sort the vector
  for (const auto& map_entry : map) {
    for (const HeaderEntryImpl& header_entry : map_entry.second) {
      output_vector.push_back(&header_entry);
    }
  }

  std::sort(output_vector.begin(), output_vector.end(),
            [](const HeaderEntryImpl* a, const HeaderEntryImpl* b) -> bool {
              // The iteration order is as follows:
              // 1. pseudo headers
              // 2. non-pseudo headers
              // Within each group, the original header addition order is preserved
              const bool a_pseudo_header =
                  !a->key().getStringView().empty() && a->key().getStringView()[0] == ':';
              const bool b_pseudo_header =
                  !b->key().getStringView().empty() && b->key().getStringView()[0] == ':';
              if (a_pseudo_header == b_pseudo_header) {
                return a->index_ < b->index_;
              }
              return a_pseudo_header ? true : false;
            });
}

void HeaderMapImpl::iterate(ConstIterateCb cb, void* context) const {
  // TODO(adisuissa): Because we need to sort the HeaderEntryImpl elements by their index,
  // we use a temp vector, populate it with the map elements, and sort by the index.
  // This can be improved by keeping a doubly linked list instead of an index,
  // and possibly splitting between pseudo-headers and headers.

  // The iteration order is as follows:
  // 1. pseudo headers
  // 2. non-pseudo headers
  // Within each group, the original header addition order is preserved

  std::vector<const HeaderEntryImpl*> temp_vec;
  temp_vec.reserve(headers_map_.size());
  mapToSortedVector(headers_map_, temp_vec);
  for (auto it = temp_vec.begin(); it != temp_vec.end(); it++) {
    if (cb(**it, context) == HeaderMap::Iterate::Break) {
      return;
    }
  }
}

void HeaderMapImpl::iterateReverse(ConstIterateCb cb, void* context) const {
  // Reverse iterate over headers, and if the callback hasn't called break
  // reverse iterate over pseudo headers.
  std::vector<const HeaderEntryImpl*> temp_vec;
  temp_vec.reserve(headers_map_.size());
  mapToSortedVector(headers_map_, temp_vec);
  for (auto it = temp_vec.rbegin(); it != temp_vec.rend(); it++) {
    if (cb(**it, context) == HeaderMap::Iterate::Break) {
      return;
    }
  }
}

HeaderMap::Lookup HeaderMapImpl::lookup(const LowerCaseString& key,
                                        const HeaderEntry** entry) const {
  // Looking up an inline header
  absl::string_view key_str = key.get();
  if (isInlineHeader(key_str)) {
    const HeaderEagerMapValue* value_list = findMapElement(key);
    if (value_list) {
      *entry = &value_list->front();
      return Lookup::Found;
    } else {
      *entry = nullptr;
      return Lookup::NotFound;
    }
  }
  *entry = nullptr;
  return Lookup::NotSupported;
}

void HeaderMapImpl::clear() {
  headers_map_.clear();
  next_header_index_ = 0;
  all_headers_num_ = 0;
  cached_byte_size_ = 0;
}

size_t HeaderMapImpl::remove(const LowerCaseString& key) {
  const auto& entry = headers_map_.find(key.get());
  if (entry == headers_map_.end()) {
    return 0;
  }

  // Subtract the sizes of the key-value pairs
  for (const auto& header_entry : entry->second) {
    subtractSize(header_entry.key().size() + header_entry.value().size());
  }

  const int removed_headers_num = entry->second.size();
  headers_map_.erase(entry);
  all_headers_num_ -= removed_headers_num;
  return removed_headers_num;
}

size_t HeaderMapImpl::removePrefix(const LowerCaseString& prefix) {
  const size_t old_size = all_headers_num_;
  absl::erase_if(
      headers_map_,
      [&prefix, this](std::pair<const absl::string_view, std::list<HeaderEntryImpl>>& map_kpv) {
        bool to_remove = absl::StartsWith(map_kpv.first, prefix.get());
        if (to_remove) {
          all_headers_num_ -= map_kpv.second.size();
          // Remove the sizes of the key-value pairs
          for (const auto& header_entry : map_kpv.second) {
            subtractSize(header_entry.key().size() + header_entry.value().size());
          }
        }
        return to_remove;
      });
  return old_size - all_headers_num_;
}

void HeaderMapImpl::dumpState(std::ostream& os, int indent_level) const {
  using IterateData = std::pair<std::ostream*, const char*>;
  const char* spaces = spacesForLevel(indent_level);
  IterateData iterate_data = std::make_pair(&os, spaces);
  iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        auto* data = static_cast<IterateData*>(context);
        *data->first << data->second << "'" << header.key().getStringView() << "', '"
                     << header.value().getStringView() << "'\n";
        return HeaderMap::Iterate::Continue;
      },
      &iterate_data);
}

const HeaderMapImpl::HeaderEagerMapValue*
HeaderMapImpl::findMapElement(const LowerCaseString& key) const {
  const auto& entry = headers_map_.find(key.get());
  if (entry == headers_map_.end()) {
    return nullptr;
  }

  return &entry->second;
}

HeaderMapImpl::HeaderEagerMapValue* HeaderMapImpl::findMapElement(const LowerCaseString& key) {
  auto entry = headers_map_.find(key.get());
  if (entry == headers_map_.end()) {
    return nullptr;
  }

  return &entry->second;
}

HeaderMapImpl::HeaderEntryImpl& HeaderMapImpl::maybeCreateInline(const LowerCaseString& key) {
  // Finds the first occurrence of a Header (if any)
  HeaderEagerMapValue* value_list = findMapElement(key);
  if (value_list) {
    ASSERT(value_list->size() == 1);
    return value_list->front();
  }

  // Create the header as inline
  addSize(key.get().size());
  HeaderString value;
  value.setCopy("");
  headers_map_[key.get()].emplace_back(std::move(key), std::move(value), next_header_index_++,
                                       true);
  all_headers_num_++;

  return headers_map_[key.get()].front();
}

HeaderEntry* HeaderMapImpl::getExisting(const LowerCaseString& key) {
  // Returns the first occurrence of a Header (if any)
  HeaderEagerMapValue* value_list = findMapElement(key);
  if (value_list == nullptr) {
    return nullptr;
  }

  return &value_list->front();
}

const HeaderMapImpl::HeaderEntryImpl*
HeaderMapImpl::getExistingInline(const LowerCaseString& key) const {
  const HeaderEagerMapValue* value_list = findMapElement(key);
  if (value_list == nullptr) {
    return nullptr;
  }

  ASSERT(value_list->size() == 1);
  return &value_list->front();
}

#define ADD_INLINE_HEADER_NAME(name) inline_headers_names_set.emplace(Headers::get().name.get());

// Initialize the per-header map inline headers names sets
const absl::flat_hash_set<absl::string_view> RequestHeaderMapImpl::inline_headers_names_set_ = [] {
  absl::flat_hash_set<absl::string_view> inline_headers_names_set;
  INLINE_REQ_HEADERS(ADD_INLINE_HEADER_NAME)
  INLINE_REQ_RESP_HEADERS(ADD_INLINE_HEADER_NAME)
  return inline_headers_names_set;
}();

const absl::flat_hash_set<absl::string_view> ResponseHeaderMapImpl::inline_headers_names_set_ = [] {
  absl::flat_hash_set<absl::string_view> inline_headers_names_set;
  INLINE_RESP_HEADERS(ADD_INLINE_HEADER_NAME)
  INLINE_REQ_RESP_HEADERS(ADD_INLINE_HEADER_NAME)
  INLINE_RESP_HEADERS_TRAILERS(ADD_INLINE_HEADER_NAME)
  return inline_headers_names_set;
}();

const absl::flat_hash_set<absl::string_view> ResponseTrailerMapImpl::inline_headers_names_set_ =
    [] {
      absl::flat_hash_set<absl::string_view> inline_headers_names_set;
      INLINE_RESP_HEADERS_TRAILERS(ADD_INLINE_HEADER_NAME)
      return inline_headers_names_set;
    }();

std::vector<HeaderMapImplUtility::HeaderMapImplInfo>
HeaderMapImplUtility::getAllHeaderMapImplInfo() {
  // No special handling for inline-headers, do nothing
  return std::vector<HeaderMapImplUtility::HeaderMapImplInfo>();
}
#endif

} // namespace Http
} // namespace Envoy
