#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/stats/scope.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

class MongoStats {
public:
  MongoStats(Stats::Scope& scope, absl::string_view prefix);

  void incCounter(const std::vector<Stats::StatName>& names);
  void recordHistogram(const std::vector<Stats::StatName>& names, Stats::Histogram::Unit unit,
                       uint64_t sample);

  /**
   * Finds or creates a StatName by string, taking a global lock if needed.
   *
   * TODO(jmarantz): Potential perf issue here with mutex contention for names
   * that have not been remembered as builtins in the constructor.
   */
  Stats::StatName getBuiltin(const std::string& str, Stats::StatName fallback) {
    return stat_name_set_->getBuiltin(str, fallback);
  }

  //Stats::StatName getDynamic(const std::string& str) { return stat_name_set_->getDynamic(str); }

  Stats::SymbolTable& symbolTable() { return scope_.symbolTable(); }

private:
  Stats::SymbolTable::StoragePtr addPrefix(const std::vector<Stats::StatName>& names);

  Stats::Scope& scope_;
  Stats::StatNameSetPtr stat_name_set_;

public:
  const Stats::StatName prefix_;
  const Stats::StatName callsite_;
  const Stats::StatName cmd_;
  const Stats::StatName collection_;
  const Stats::StatName multi_get_;
  const Stats::StatName reply_num_docs_;
  const Stats::StatName reply_size_;
  const Stats::StatName reply_time_ms_;
  const Stats::StatName time_ms_;
  const Stats::StatName query_;
  const Stats::StatName scatter_get_;
  const Stats::StatName total_;
  const Stats::StatName unknown_command_;
};
using MongoStatsSharedPtr = std::shared_ptr<MongoStats>;

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
