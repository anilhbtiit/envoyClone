#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>

#include "envoy/common/optref.h"

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_set.h"

namespace Envoy {

/**
 * This file contains classes to help make very fast maps where a subset frequently used keys are
 * known before the map is constructed.
 *
 * For example, the filter state always uses the filter name as the key and the filter name is
 * known at compile time.
 * By using the these classes, the filter state could get the key/value pair without any hash.
 *
 * Dynamic key lookups are also supported and have slightly worse performance than
 * absl::flat_hash_map. Using this class makes sense if most of the lookups are expected to use the
 * predefined handles.
 *
 *
 * Example:
 *
 *   // Establishes a descriptor defining fast handles for the inline map. The descriptor must
 *   // outlive maps defined using it. One possible usage model is to lazy-create statics for a
 *   // descriptor and its handles using MUTABLE_CONSTRUCT_ON_FIRST_USE.
 *   InlineMapDescriptor<std::string> descriptor;
 *
 *   // Create the handle for inline key. We should never do this after bootstrapping. Typically, we
 *   // can do this by define a global variable.
 *   InlineMapDescriptor<std::string>::Handle handle = descriptor.addHandle("inline_key");
 *
 *   // Finalize the descriptor. No further changes are allowed to the descriptor after this point.
 *   descriptor.finalize();
 *
 *   // Create the inline map.
 *   InlineMapPtr<std::string, std::string> inline_map = InlineMap<std::string,
 *     std::string>::create(descriptor);
 *
 *   // Get value by handle.
 *   inline_map->insert(handle, "value");
 *   EXPECT_EQ(*inline_map->lookup(handle), "value");
 */

/**
 * Maintains a collection of lightweight handles use for fast lookups into an inline map.
 */
template <class StorageKey> class InlineMapDescriptor {
public:
  /**
   * Holds a fast index to a map. This is guaranteed to be small and cheap to copy and store.
   */
  class Handle {
  public:
    /**
     * Get the id of the inline entry in the inline array. This could be used to access the
     * key/value pair in the inline map without hash searching.
     */
    uint32_t inlineId() const { return inline_id_; }

  private:
    friend class InlineMapDescriptor;

    // This constructor should only be called by InlineMapDescriptor.
    Handle(uint32_t inline_id) : inline_id_(inline_id) {}

    uint32_t inline_id_{};
  };

  using InlineKeysMap = absl::flat_hash_map<StorageKey, Handle>;
  using InlineKeys = std::vector<StorageKey>;

  InlineMapDescriptor() = default;

  /**
   * Add an inline key and return related handle. May only be called before finalize().
   * Heterogeneous lookup is supported here.
   */
  template <class LookupKey> Handle addInlineKey(const LookupKey& key) {
    RELEASE_ASSERT(!finalized_, "Cannot create new inline key after finalize()");

    if (auto it = inline_keys_map_.find(key); it != inline_keys_map_.end()) {
      // If the key is already added, return related inline handle directly.
      return it->second;
    } else {
      // If the key is not added yet, then create a new handle for this key.
      ASSERT(inline_keys_map_.size() < std::numeric_limits<uint32_t>::max());
      Handle handle{static_cast<uint32_t>(inline_keys_map_.size())};
      inline_keys_.push_back(StorageKey(key));
      inline_keys_map_.emplace(key, handle);
      return handle;
    }
  }

  /**
   * Fetch the handle for the given key. Return handle if the given key is inline key and return
   * absl::nullopt if the given key is normal key. May only be called after finalize(). This should
   * be used to get the handle of the inline keys that added by addHandle(). This function could
   * used to determine if a key is added as inline key or not at runtime or xDS config loading time
   * and decide if the key should be used as inline key or normal key.
   * Heterogeneous lookup is supported here.
   */
  template <class LookupKey> absl::optional<Handle> getHandleByKey(const LookupKey& key) const {
    ASSERT(finalized_, "Cannot get inline handle before finalize()");

    if (auto it = inline_keys_map_.find(key); it == inline_keys_map_.end()) {
      return absl::nullopt;
    } else {
      return it->second;
    }
  }

  /**
   * Get the inline keys map that contains all inline keys and their handle. May only be called
   * after finalize().
   */
  const InlineKeysMap& inlineKeysMap() const {
    ASSERT(finalized_, "Cannot fetch registration map before finalize()");
    return inline_keys_map_;
  }

  /**
   * Get the array that contains all added inline keys. May only be called after finalize().
   */
  const InlineKeys& inlineKeys() const {
    ASSERT(finalized_, "Cannot fetch registration set before finalize()");
    return inline_keys_;
  }

  /**
   * Get the number of the inline keys in this descriptor. May only be called after finalize().
   */
  uint64_t inlineKeysNum() const {
    ASSERT(finalized_, "Cannot fetch registration map before finalize()");
    return inline_keys_map_.size();
  }

  /**
   * Finalize this descriptor. No further changes are allowed after this point. This guaranteed that
   * all map created by the process have the same variable size and custom inline key adding. This
   * method should only be called once.
   */
  void finalize() {
    ASSERT(!finalized_, "Cannot finalize() an already finalized descriptor");
    finalized_ = true;
  }

  /**
   * @return true if the descriptor is finalized.
   */
  bool finalized() const { return finalized_; }

private:
  bool finalized_{};
  InlineKeys inline_keys_;
  InlineKeysMap inline_keys_map_;
};

/**
 * This is the inline map that could be used as an alternative of normal hash map to store the
 * key/value pairs.
 */
template <class StorageKey, class StorageValue> class InlineMap : public InlineStorage {
public:
  using TypedInlineMapDescriptor = InlineMapDescriptor<StorageKey>;
  using Handle = typename TypedInlineMapDescriptor::Handle;

  using DynamicHashMap = absl::flat_hash_map<StorageKey, StorageValue>;
  using StorageValueRef = std::reference_wrapper<StorageValue>;

  /**
   * Get the entry by the given key. Heterogeneous lookup is supported here.
   * @param key the key to lookup.
   * @return the entry if the key is found, otherwise return null reference.
   */
  template <class LookupKey> OptRef<const StorageValue> lookup(const LookupKey& key) const {
    // Because the dynamic key is used here, try the normal map entry first.
    if (auto it = dynamic_entries_.find(key); it != dynamic_entries_.end()) {
      return it->second;
    }

    if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
      if (inlineEntryValid(*entry_id)) {
        return inline_entries_[*entry_id];
      }
    }

    return {};
  }

  /**
   * Get the entry by the given key. Heterogeneous lookup is supported here.
   * @param key the key to lookup.
   * @return the entry if the key is found, otherwise return null reference.
   */
  template <class LookupKey> OptRef<StorageValue> lookup(const LookupKey& key) {
    // Because the normal string view key is used here, try the normal map entry first.
    if (auto it = dynamic_entries_.find(key); it != dynamic_entries_.end()) {
      return it->second;
    }

    if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
      if (inlineEntryValid(*entry_id)) {
        return inline_entries_[*entry_id];
      }
    }

    return {};
  }

  /**
   * Get the entry by the given handle.
   * @param handle the handle to lookup.
   * @return the entry if the handle is valid, otherwise return null reference.
   */
  OptRef<const StorageValue> lookup(Handle handle) const {
    if (inlineEntryValid(handle.inlineId())) {
      return inline_entries_[handle.inlineId()];
    }
    return {};
  }

  /**
   * Get the entry by the given handle.
   * @param handle the handle to lookup.
   * @return the entry if the handle is valid, otherwise return null reference.
   */
  OptRef<StorageValue> lookup(Handle handle) {
    if (inlineEntryValid(handle.inlineId())) {
      return inline_entries_[handle.inlineId()];
    }
    return {};
  }

  /**
   * Set the entry by the given key.
   * @param key the key to set.
   * @param value the value to set.
   * @return pair consisting of a reference to the inserted element, or the already-existing element
   * if no insertion happened, and a bool denoting whether the insertion took place (true if
   * insertion happened, false if it did not).
   */
  template <class LookupKey, class Value>
  std::pair<StorageValueRef, bool> insert(LookupKey&& key, Value&& value) {
    if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
      // This key is registered as inline key and try to insert the value to the inline array.

      // If the entry is already valid, insert will fail and return the ref of exist value.
      if (inlineEntryValid(*entry_id)) {
        return {inline_entries_[*entry_id], false};
      }

      resetInlineMapEntry(*entry_id, std::forward<Value>(value));
      return {inline_entries_[*entry_id], true};
    } else {
      // This key is not registered as inline key and try to insert the value to the normal map.
      auto result =
          dynamic_entries_.emplace(std::forward<LookupKey>(key), std::forward<Value>(value));
      return {result.first->second, result.second};
    }
  }

  /**
   * Set the entry by the given handle.
   * @param handle the handle to set.
   * @param value the value to set.
   * @return pair consisting of a reference to the inserted element, or the already-existing element
   * if no insertion happened, and a bool denoting whether the insertion took place (true if
   * insertion happened, false if it did not).
   */
  template <class Value> std::pair<StorageValueRef, bool> insert(Handle handle, Value&& value) {
    // If the entry is already valid, insert will fail and return false.
    if (inlineEntryValid(handle.inlineId())) {
      return {inline_entries_[handle.inlineId()], false};
    }

    resetInlineMapEntry(handle.inlineId(), std::forward<Value>(value));
    return {inline_entries_[handle.inlineId()], true};
  }

  /**
   * Remove the entry by the given key. If the key is not found, do nothing.
   * Heterogeneous lookup is supported here.
   * @param key the key to remove.
   * @return the number of elements removed.
   */
  template <class LookupKey> uint64_t remove(const LookupKey& key) {
    if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
      return clearInlineMapEntry(*entry_id);
    } else {
      return dynamic_entries_.erase(key);
    }
  }

  // Remove the entry by the given handle. If the handle is not valid, do nothing.
  void remove(Handle handle) { clearInlineMapEntry(handle.inlineId()); }

  void iterate(std::function<bool(const StorageValue&, const StorageValue&)> callback) const {
    for (const auto& entry : dynamic_entries_) {
      if (!callback(entry.first, entry.second)) {
        return;
      }
    }

    const uint64_t inline_keys_num = descriptor_.inlineKeysNum();
    const auto& inline_keys = descriptor_.inlineKeys();
    for (uint64_t i = 0; i < inline_keys_num; ++i) {
      if (!inlineEntryValid(i)) {
        continue;
      }

      if (!callback(inline_keys[i], inline_entries_[i])) {
        return;
      }
    }
  }

  /**
   * @return the number of elements in the map.
   */
  uint64_t size() const { return dynamic_entries_.size() + inline_entries_size_; }

  /**
   * @return true if the map is empty.
   */
  bool empty() const { return size() == 0; }

  ~InlineMap() {
    // Destruct all inline entries.
    for (uint64_t i = 0; i < descriptor_.inlineKeysNum(); ++i) {
      clearInlineMapEntry(i);
    }
  };

  // Override operator [] to support the normal key assignment.
  template <class ArgK> StorageValue& operator[](const ArgK& key) {
    if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
      // This key is registered as inline key and try to insert the value to the inline array.
      if (!inlineEntryValid(*entry_id)) {
        resetInlineMapEntry(*entry_id, StorageValue());
      }
      return inline_entries_[*entry_id];
    } else {
      // This key is not registered as inline key and try to insert the value to the normal map.
      return dynamic_entries_[key];
    }
  }

  // Override operator [] to support the handle of inline key assignment.
  StorageValue& operator[](Handle handle) {
    if (!inlineEntryValid(handle.inlineId())) {
      resetInlineMapEntry(handle.inlineId(), StorageValue());
    }
    return inline_entries_[handle.inlineId()];
  }

  static std::unique_ptr<InlineMap> create(TypedInlineMapDescriptor& registry) {
    if (!registry.finalized()) {
      // Call finalize() to make sure that the registry is finalized and no any new inline
      // keys could be registered.
      registry.finalize();
    }

    return std::unique_ptr<InlineMap>(new ((registry.inlineKeysNum() * sizeof(StorageValue)))
                                          InlineMap(registry));
  }

private:
  friend class InlineMapDescriptor<StorageKey>;

  InlineMap(TypedInlineMapDescriptor& registry) : descriptor_(registry) {
    // Initialize the inline entries to nullptr.
    uint64_t inline_keys_num = descriptor_.inlineKeysNum();
    inline_entries_valid_.resize(inline_keys_num, false);
    memset(inline_entries_storage_, 0, inline_keys_num * sizeof(StorageValue));
    inline_entries_ = reinterpret_cast<StorageValue*>(inline_entries_storage_);
  }

  template <class ArgV> void resetInlineMapEntry(uint64_t inline_entry_id, ArgV&& value) {
    ASSERT(inline_entry_id < descriptor_.inlineKeysNum());

    clearInlineMapEntry(inline_entry_id);

    // Construct the new entry in the inline array.
    new (inline_entries_ + inline_entry_id) StorageValue(std::forward<ArgV>(value));
    setInlineEntryValid(inline_entry_id, true);
  }

  uint64_t clearInlineMapEntry(uint64_t inline_entry_id) {
    ASSERT(inline_entry_id < descriptor_.inlineKeysNum());

    // Destruct the old entry if it is valid.
    if (inlineEntryValid(inline_entry_id)) {
      (inline_entries_ + inline_entry_id)->~StorageValue();
      setInlineEntryValid(inline_entry_id, false);
      return 1;
    }

    return 0;
  }

  template <class ArgK> absl::optional<uint64_t> inlineLookup(const ArgK& key) const {
    const auto& map_ref = descriptor_.inlineKeysMap();
    if (auto iter = map_ref.find(key); iter != map_ref.end()) {
      return iter->second.inlineId();
    }
    return absl::nullopt;
  }

  bool inlineEntryValid(uint64_t inline_entry_id) const {
    ASSERT(inline_entry_id < descriptor_.inlineKeysNum());
    return inline_entries_valid_[inline_entry_id];
  }
  void setInlineEntryValid(uint64_t inline_entry_id, bool flag) {
    ASSERT(inline_entry_id < descriptor_.inlineKeysNum());

    if (flag) {
      inline_entries_size_++;
    } else {
      ASSERT(inline_entries_size_ > 0);
      inline_entries_size_--;
    }

    inline_entries_valid_[inline_entry_id] = flag;
  }

  // This is the reference of the registry that the inline map created by. This is used to
  // validate the inline handle validity and get the inline key set.
  const TypedInlineMapDescriptor& descriptor_;

  // This is the underlay hash map for the dynamic map entries.
  DynamicHashMap dynamic_entries_;

  // This is flags to indicate if the inline entries are valid.
  // TODO(wbpcode): this will add additional one time memory allocation when constructing inline
  // map. It is possible to use memory in the inline_entries_storage_ to store the flags in the
  // future. But it should be fine for now.
  std::vector<bool> inline_entries_valid_;

  uint64_t inline_entries_size_{};

  StorageValue* inline_entries_{};

  // This should be the last member of the class and no member should be added after this.
  uint8_t inline_entries_storage_[];
};

template <class Key, class Value> using InlineMapPtr = std::unique_ptr<InlineMap<Key, Value>>;

} // namespace Envoy
