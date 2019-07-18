#pragma once

#include <memory>
#include <string>

#include "envoy/stats/scope.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

class DynamoStats {
public:
  DynamoStats(Stats::Scope& scope, const std::string& prefix);
  Stats::SymbolTable& symbolTable() { return scope_.symbolTable(); }

  Stats::Counter& counter(const std::vector<Stats::StatName>& names);
  Stats::Histogram& histogram(const std::vector<Stats::StatName>& names);

  /**
   * Creates the partition id stats string.
   * The stats format is
   * "<stat_prefix>table.<table_name>.capacity.<operation>.__partition_id=<partition_id>".
   * Partition ids and dynamodb table names can be long. To satisfy the string length,
   * we truncate in two ways:
   * 1. We only take the last 7 characters of the partition id.
   * 2. If the stats string with <table_name> is longer than the stats MAX_NAME_SIZE, we will
   * truncate the table name to
   * fit the size requirements.
   */
  Stats::Counter& buildPartitionStatCounter(const std::string& table_name,
                                            const std::string& operation,
                                            const std::string& partition_id);

  static size_t groupIndex(uint64_t status);

  Stats::StatName getStatName(const std::string& str);

private:
  Stats::SymbolTable::StoragePtr addPrefix(const std::vector<Stats::StatName>& names);

  Stats::Scope& scope_;
  Stats::StatNamePool pool_ GUARDED_BY(mutex_);
  const Stats::StatName prefix_;
  absl::Mutex mutex_;
  using StringStatNameMap = absl::flat_hash_map<std::string, Stats::StatName>;
  StringStatNameMap builtin_stat_names_;
  StringStatNameMap dynamic_stat_names_ GUARDED_BY(mutex_);

public:
  const Stats::StatName batch_failure_unprocessed_keys_;
  const Stats::StatName capacity_;
  const Stats::StatName empty_response_body_;
  const Stats::StatName error_;
  const Stats::StatName invalid_req_body_;
  const Stats::StatName invalid_resp_body_;
  const Stats::StatName multiple_tables_;
  const Stats::StatName no_table_;
  const Stats::StatName operation_missing_;
  const Stats::StatName table_;
  const Stats::StatName table_missing_;
  const Stats::StatName upstream_rq_time_;
  const Stats::StatName upstream_rq_total_;
  const Stats::StatName upstream_rq_unknown_;

  // Keep group codes for HTTP status codes through the 500s.
  static constexpr size_t NumGroupEntries = 6;
  Stats::StatName upstream_rq_total_groups_[NumGroupEntries];
  Stats::StatName upstream_rq_time_groups_[NumGroupEntries];
};
using DynamoStatsSharedPtr = std::shared_ptr<DynamoStats>;

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
