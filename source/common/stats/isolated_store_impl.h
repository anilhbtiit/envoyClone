#pragma once

#include <string.h>

#include <algorithm>
#include <string>

#include "envoy/stats/stats.h"
#include "envoy/stats/stats_options.h"
#include "envoy/stats/store.h"

#include "common/common/utility.h"
#include "common/stats/heap_stat_data.h"
#include "common/stats/stats_options_impl.h"
#include "common/stats/symbol_table_impl.h"
#include "common/stats/utility.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Stats {

/**
 * A stats cache template that is used by the isolated store.
 */
template <class Base> class IsolatedStatsCache {
public:
  using Allocator = std::function<std::shared_ptr<Base>(StatName name)>;

  IsolatedStatsCache(Allocator alloc) : alloc_(alloc) {}

  Base& get(StatName name) {
    auto stat = stats_.find(name);
    if (stat != stats_.end()) {
      return *stat->second;
    }

    std::shared_ptr<Base> new_stat = alloc_(name);
    stats_.emplace(new_stat->statName(), new_stat);
    return *new_stat;
  }

  std::vector<std::shared_ptr<Base>> toVector() const {
    std::vector<std::shared_ptr<Base>> vec;
    vec.reserve(stats_.size());
    for (auto& stat : stats_) {
      vec.push_back(stat.second);
    }

    return vec;
  }

  // void clear() { stats_.clear(); }

private:
  StatNameHashMap<std::shared_ptr<Base>> stats_;
  Allocator alloc_;
};

class IsolatedStoreImpl : public Store {
public:
  IsolatedStoreImpl();
  explicit IsolatedStoreImpl(SymbolTable& symbol_table);

  // Stats::Scope
  Counter& counterFromStatName(StatName name) override { return counters_.get(name); }
  ScopePtr createScope(const std::string& name) override;
  void deliverHistogramToSinks(const Histogram&, uint64_t) override {}
  Gauge& gaugeFromStatName(StatName name) override { return gauges_.get(name); }
  BoolIndicator& boolIndicatorFromStatName(StatName name) override {
    return bool_indicators_.get(name);
  }
  Histogram& histogramFromStatName(StatName name) override { return histograms_.get(name); }
  const Stats::StatsOptions& statsOptions() const override { return stats_options_; }
  const SymbolTable& symbolTable() const override { return symbol_table_; }
  virtual SymbolTable& symbolTable() override { return symbol_table_; }

  // Stats::Store
  std::vector<CounterSharedPtr> counters() const override { return counters_.toVector(); }
  std::vector<GaugeSharedPtr> gauges() const override { return gauges_.toVector(); }
  std::vector<BoolIndicatorSharedPtr> boolIndicators() const override {
    return bool_indicators_.toVector();
  }
  std::vector<ParentHistogramSharedPtr> histograms() const override {
    return std::vector<ParentHistogramSharedPtr>{};
  }

  Counter& counter(const std::string& name) override {
    StatNameTempStorage storage(name, symbolTable());
    return counterFromStatName(storage.statName());
  }
  Gauge& gauge(const std::string& name) override {
    StatNameTempStorage storage(name, symbolTable());
    return gaugeFromStatName(storage.statName());
  }
  BoolIndicator& boolIndicator(const std::string& name) override {
    StatNameTempStorage storage(name, symbolTable());
    return boolIndicatorFromStatName(storage.statName());
  }
  Histogram& histogram(const std::string& name) override {
    StatNameTempStorage storage(name, symbolTable());
    return histogramFromStatName(storage.statName());
  }

  //void clear();

private:
  IsolatedStoreImpl(std::unique_ptr<SymbolTable>&& symbol_table);

  std::unique_ptr<SymbolTable> symbol_table_storage_;
  SymbolTable& symbol_table_;
  HeapStatDataAllocator alloc_;
  IsolatedStatsCache<Counter> counters_;
  IsolatedStatsCache<Gauge> gauges_;
  IsolatedStatsCache<BoolIndicator> bool_indicators_;
  IsolatedStatsCache<Histogram> histograms_;
  const StatsOptionsImpl stats_options_;
};

} // namespace Stats
} // namespace Envoy
