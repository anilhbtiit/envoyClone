#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace Envoy {

/**
 * This is a specialized structure intended for static header maps, but
 * there may be other use cases.
 * The structure is:
 * 1. a length-based lookup table so only keys the same length as the
 * target key are considered.
 * 2. a trie that branches on the "most divisions" position of the key.
 *
 * For example, if we consider the case where the set of headers is
 * `x-prefix-banana`
 * `x-prefix-babana`
 * `x-prefix-apple`
 * `x-prefix-pineapple`
 * `x-prefix-barana`
 * `x-prefix-banaka`
 *
 * A standard front-first trie looking for `x-prefix-banana` would walk
 * 7 nodes through the tree, first for `x`, then for `-`, etc.
 *
 * This structure first jumps to matching length, eliminating in this
 * example case apple and pineapple.
 * Then the "best split" node is on
 *   `x-prefix-banana`
 *               ^
 * so the first node has 3 non-miss branches, n, b and r for that position.
 * Down that n branch, the "best split" is on
 *   `x-prefix-banana`
 *                 ^
 * which has two branches, n or k.
 * Down the n branch is the leaf node (only `x-prefix-banana` remains) - at
 * this point a regular string-compare checks if the key is an exact match
 * for the string node.
 */
template <class Value> class CompiledStringMap {
  using FindFn = std::function<Value(const absl::string_view&)>;

public:
  using KV = std::pair<absl::string_view, Value>;
  /**
   * Returns the value with a matching key, or the default value
   * (typically nullptr) if the key was not present.
   * @param key the key to look up.
   */
  Value find(const absl::string_view& key) const {
    if (key.size() >= table_.size() || table_[key.size()] == nullptr) {
      return {};
    }
    return table_[key.size()](key);
  };
  /**
   * Construct the lookup table. This is a somewhat slow multi-pass
   * operation - using this structure is not recommended unless the
   * table is initialize-once, use-many.
   * @param initial a vector of key->value pairs. This is taken by value because
   *                we're going to modify it. If the caller still wants the original
   *                then it can be copied in, if not it can be moved in.
   */
  void compile(std::vector<KV> initial) {
    if (initial.empty()) {
      return;
    }
    std::sort(initial.begin(), initial.end(),
              [](const KV& a, const KV& b) { return a.first.size() < b.first.size(); });
    size_t longest = initial.back().first.size();
    table_.resize(longest + 1);
    auto range_start = initial.begin();
    // Populate the subnodes for each length of key that exists.
    while (range_start != initial.end()) {
      // Find the first key whose length differs from the current key length.
      // Everything in between is keys with the same length.
      auto range_end =
          std::find_if(range_start, initial.end(), [l = range_start->first.size()](const KV& e) {
            return e.first.size() != l;
          });
      std::vector<KV> node_contents;
      // Populate a FindFn for the nodes in that range.
      node_contents.reserve(range_end - range_start);
      std::copy(range_start, range_end, std::back_inserter(node_contents));
      table_[range_start->first.size()] = createEqualLengthNode(node_contents);
      range_start = range_end;
    }
  }

private:
  /*
   * @param node_contents the set of key-value pairs that will
   */
  static FindFn createEqualLengthNode(std::vector<KV> node_contents) {
    if (node_contents.size() == 1) {
      return [pair = std::make_pair<std::string, Value>(std::string{node_contents[0].first},
                                                        std::move(node_contents[0].second))](
                 const absl::string_view& key) -> Value {
        if (key != pair.first) {
          return {};
        }
        return pair.second;
      };
    }
    struct IndexSplitInfo {
      uint8_t index, min, max, count;
    } best{0, 0, 0, 0};
    for (size_t i = 0; i < node_contents[0].first.size(); i++) {
      std::array<bool, 256> hits{};
      IndexSplitInfo info{static_cast<uint8_t>(i), 255, 0, 0};
      for (size_t j = 0; j < node_contents.size(); j++) {
        uint8_t v = node_contents[j].first[i];
        if (!hits[v]) {
          hits[v] = true;
          info.count++;
          info.min = std::min(v, info.min);
          info.max = std::max(v, info.max);
        }
      }
      if (info.count > best.count) {
        best = info;
      }
    }
    std::vector<FindFn> nodes;
    nodes.resize(best.max - best.min + 1);
    std::sort(node_contents.begin(), node_contents.end(), [&best](const KV& a, const KV& b) {
      return a.first[best.index] < b.first[best.index];
    });
    auto range_start = node_contents.begin();
    while (range_start != node_contents.end()) {
      // Find the first key whose character at position [best.index] differs from the
      // character of the current range.
      // Everything in between is keys with the same character at this index.
      auto range_end = std::find_if(range_start, node_contents.end(),
                                    [index = best.index, c = range_start->first[best.index]](
                                        const KV& e) { return e.first[index] != c; });
      // Possible optimization was tried here, std::array<KV, 256> rather than
      // a smaller-range vector with bounds, to keep locality and reduce
      // comparisons. It didn't help.
      std::vector<KV> next_contents;
      next_contents.reserve(range_end - range_start);
      std::copy(range_start, range_end, std::back_inserter(next_contents));
      nodes[range_start->first[best.index] - best.min] = createEqualLengthNode(next_contents);
      range_start = range_end;
    }
    return [nodes = std::move(nodes), min = best.min,
            index = best.index](const absl::string_view& key) -> Value {
      uint8_t k = static_cast<uint8_t>(key[index]);
      // Possible optimization was tried here, populating empty nodes with
      // a function that returns {} to reduce branching vs checking for null
      // nodes. Checking for null nodes benchmarked faster.
      if (k < min || k >= min + nodes.size() || nodes[k - min] == nullptr) {
        return {};
      }
      return nodes[k - min](key);
    };
  }
  std::vector<FindFn> table_;
};

} // namespace Envoy
