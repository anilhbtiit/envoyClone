#pragma once

#include "envoy/stream_info/set_filter_state_object.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace StreamInfo {

/**
 * Implementation of SetFilterStateObject.
 */
template <typename T> class SetFilterStateObjectImpl : public SetFilterStateObject<T> {
public:
  void add(const T& address) override { values__.emplace(address); }

  void clear() override { values__.clear(); }

  void iterate(const std::function<bool(const T& address)>& fn) const override {
    for (const auto& address : values__) {
      if (!fn(address)) {
        break;
      }
    }
  }

  static const std::string& key() {
    CONSTRUCT_ON_FIRST_USE(std::string, "filter_state_key.set_object");
  }

private:
  absl::flat_hash_set<T> values__;
};

} // namespace StreamInfo
} // namespace Envoy
