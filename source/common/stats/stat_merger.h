#pragma once

#include "envoy/stats/store.h"

#include "common/protobuf/protobuf.h"
#include "common/stats/symbol_table_impl.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Stats {

// Responsible for the sensible merging of two instances of the same stat from two different
// (typically hot restart parent+child) Envoy processes.
class StatMerger {
public:
  using DynamicsMap = absl::flat_hash_map<std::string, DynamicSpans>;

  // Holds state needed to construct StatName with mixed dynamic/symbolic
  // components, based on a map.
  class DynamicContext {
  public:
    DynamicContext(SymbolTable& symbol_table)
        : symbol_table_(symbol_table), symbolic_pool_(symbol_table), dynamic_pool_(symbol_table) {}

    /**
     * Generates a StatName with mixed dynamic/symbolic components based on
     * the string and the dynamic_map obtained from encodeSegments.
     *
     * @param name The string corresponding to the desired StatName.
     * @param map a map indicating which spans of tokens in the stat-name are dynamic.
     * @return the generated StatName, valid as long as the DynamicContext.
     */
    StatName makeDynamicStatName(const std::string& name, const DynamicsMap& map);

  private:
    SymbolTable& symbol_table_;
    StatNamePool symbolic_pool_;
    StatNameDynamicPool dynamic_pool_;
    SymbolTable::StoragePtr storage_ptr_;
  };

  StatMerger(Stats::Store& target_store);

  /**
   * Merge the values of stats_proto into stats_store. Counters are always
   * straightforward addition, while gauges default to addition but have
   * exceptions.
   *
   * @param counter_deltas map of counter changes from parent
   * @param counter_deltas map of gauge changes from parent
   * @param dynamics information about which segments of the names are dynamic.
   */
  void mergeStats(const Protobuf::Map<std::string, uint64_t>& counter_deltas,
                  const Protobuf::Map<std::string, uint64_t>& gauges,
                  const DynamicsMap& dynamics = DynamicsMap());

  /**
   * By the time a parent exits, all its contributions to accumulated gauges
   * should be zero. But depending on the timing of the stat-merger communication
   * shutdown and other shutdown activities on the parent, the gauges may not
   * all be zero yet. So simply erase all the parent contributions.
   */
  void removeParentContributionToGauges();

  void dropParentGaugeValue(Stats::StatName gauge_name);

private:
  void mergeCounters(const Protobuf::Map<std::string, uint64_t>& counter_deltas,
                     const DynamicsMap& dynamics_map);
  void mergeGauges(const Protobuf::Map<std::string, uint64_t>& gauges,
                   const DynamicsMap& dynamics_map);

  StatNameHashMap<uint64_t> parent_gauge_values_;
  // A stats Scope for our in-the-merging-process counters to live in. Scopes conceptually hold
  // shared_ptrs to the stats that live in them, with the question of which stats are living in a
  // given scope determined by which stat names have been accessed via that scope. E.g., if you
  // access a stat named "some.shared" directly through the ordinary store, and then access a
  // stat named "shared" in a scope configured with the prefix "some.", there is now a single
  // stat named some.shared pointed to by both. As another example, if you access the stat
  // "single" in the "some" scope, there will be a stat named "some.single" pointed to by just
  // that scope. Now, if you delete the scope, some.shared will stick around, but some.single
  // will be destroyed.
  //
  // All of that is relevant here because it is used to get a certain desired behavior.
  // Specifically, stats must be kept up to date with values from the parent throughout hot
  // restart, but once the restart completes, they must be dropped without a trace if the child has
  // not taken action (independent of the hot restart stat merging) that would lead to them getting
  // created in the store. By storing these stats in a scope (with an empty prefix), we can
  // preserve all stats throughout the hot restart. Then, when the restart completes, dropping
  // the scope will drop exactly those stats whose names have not already been accessed through
  // another store/scope.
  ScopePtr temp_scope_;
};

} // namespace Stats
} // namespace Envoy
