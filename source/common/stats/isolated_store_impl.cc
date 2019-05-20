#include "common/stats/isolated_store_impl.h"

#include <string.h>

#include <algorithm>
#include <string>

#include "common/common/utility.h"
#include "common/stats/fake_symbol_table_impl.h"
#include "common/stats/histogram_impl.h"
#include "common/stats/scope_prefixer.h"
#include "common/stats/utility.h"

namespace Envoy {
namespace Stats {

IsolatedStoreImpl::IsolatedStoreImpl()
    : IsolatedStoreImpl(std::make_unique<FakeSymbolTableImpl>()) {}

IsolatedStoreImpl::IsolatedStoreImpl(std::unique_ptr<SymbolTable>&& symbol_table)
    : IsolatedStoreImpl(*symbol_table) {
  symbol_table_storage_ = std::move(symbol_table);
}

IsolatedStoreImpl::IsolatedStoreImpl(SymbolTable& symbol_table)
    : StoreImpl(symbol_table), alloc_(symbol_table),
      counters_([this](StatName name) -> CounterSharedPtr {
        return alloc_.makeCounter(name, alloc_.symbolTable().toString(name), std::vector<Tag>());
      }),
      gauges_([this](StatName name) -> GaugeSharedPtr {
        return alloc_.makeGauge(name, alloc_.symbolTable().toString(name), std::vector<Tag>());
      }),
      histograms_([this](StatName name) -> HistogramSharedPtr {
        return std::make_shared<HistogramImpl>(name, *this, alloc_.symbolTable().toString(name),
                                               std::vector<Tag>());
      }),
      null_gauge_(symbol_table) {}

IsolatedStoreImpl::~IsolatedStoreImpl() { stat_name_set_.free(symbolTable()); }

ScopePtr IsolatedStoreImpl::createScope(const std::string& name) {
  return std::make_unique<ScopePrefixer>(name, *this);
}

StatName IsolatedStoreImpl::fastMemoryIntensiveStatNameLookup(absl::string_view name) {
  absl::optional<StatName> stat_name = string_stat_name_map_.find(name, symbolTable());
  if (!stat_name) {
    StatNameStorage storage(name, symbolTable());
    auto insertion = stat_name_set_.insert(std::move(storage));
    ASSERT(insertion.second); // If the name is not in the map, it should not be in the set.
    stat_name = insertion.first->statName();
  }
  return *stat_name;
}

} // namespace Stats
} // namespace Envoy
