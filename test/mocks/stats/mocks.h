#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

#include "envoy/stats/histogram.h"
#include "envoy/stats/sink.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_matcher.h"
#include "envoy/stats/store.h"
#include "envoy/stats/timespan.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/stats/fake_symbol_table_impl.h"
#include "common/stats/histogram_impl.h"
#include "common/stats/isolated_store_impl.h"
#include "common/stats/store_impl.h"
#include "common/stats/symbol_table_creator.h"
#include "common/stats/timespan_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/global.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Stats {

class TestSymbolTableHelper {
public:
  TestSymbolTableHelper() : symbol_table_(SymbolTableCreator::makeSymbolTable()) {}
  SymbolTable& symbolTable() { return *symbol_table_; }
  const SymbolTable& constSymbolTable() const { return *symbol_table_; }

private:
  SymbolTablePtr symbol_table_;
};

class TestSymbolTable {
public:
  SymbolTable& operator*() { return global_.get().symbolTable(); }
  const SymbolTable& operator*() const { return global_.get().constSymbolTable(); }
  SymbolTable* operator->() { return &global_.get().symbolTable(); }
  const SymbolTable* operator->() const { return &global_.get().constSymbolTable(); }
  Envoy::Test::Global<TestSymbolTableHelper> global_;
};

template <class BaseClass> class MockMetric : public BaseClass {
public:
  MockMetric() : name_(*this), tag_pool_(*symbol_table_) {}
  ~MockMetric() override = default;

  // This bit of C++ subterfuge allows us to support the wealth of tests that
  // do metric->name_ = "foo" even though names are more complex now. Note
  // that the statName is only populated if there is a symbol table.
  class MetricName {
  public:
    explicit MetricName(MockMetric& mock_metric) : mock_metric_(mock_metric) {}
    ~MetricName() {
      if (stat_name_storage_ != nullptr) {
        stat_name_storage_->free(mock_metric_.symbolTable());
      }
    }

    void operator=(absl::string_view str) {
      name_ = std::string(str);
      stat_name_storage_ = std::make_unique<StatNameStorage>(str, mock_metric_.symbolTable());
    }

    std::string name() const { return name_; }
    StatName statName() const { return stat_name_storage_->statName(); }

  private:
    MockMetric& mock_metric_;
    std::string name_;
    std::unique_ptr<StatNameStorage> stat_name_storage_;
  };

  SymbolTable& symbolTable() override { return *symbol_table_; }
  const SymbolTable& constSymbolTable() const override { return *symbol_table_; }

  // Note: cannot be mocked because it is accessed as a Property in a gmock EXPECT_CALL. This
  // creates a deadlock in gmock and is an unintended use of mock functions.
  std::string name() const override { return name_.name(); }
  StatName statName() const override { return name_.statName(); }
  std::vector<Tag> tags() const override { return tags_; }
  void setTagExtractedName(absl::string_view name) {
    tag_extracted_name_ = std::string(name);
    tag_extracted_stat_name_ =
        std::make_unique<StatNameManagedStorage>(tagExtractedName(), *symbol_table_);
  }
  std::string tagExtractedName() const override {
    return tag_extracted_name_.empty() ? name() : tag_extracted_name_;
  }
  StatName tagExtractedStatName() const override { return tag_extracted_stat_name_->statName(); }
  void iterateTagStatNames(const Metric::TagStatNameIterFn& fn) const override {
    ASSERT((tag_names_and_values_.size() % 2) == 0);
    for (size_t i = 0; i < tag_names_and_values_.size(); i += 2) {
      if (!fn(tag_names_and_values_[i], tag_names_and_values_[i + 1])) {
        return;
      }
    }
  }

  TestSymbolTable symbol_table_; // Must outlive name_.
  MetricName name_;

  void setTags(const std::vector<Tag>& tags) {
    tag_pool_.clear();
    tags_ = tags;
    for (const Tag& tag : tags) {
      tag_names_and_values_.push_back(tag_pool_.add(tag.name_));
      tag_names_and_values_.push_back(tag_pool_.add(tag.value_));
    }
  }
  void addTag(const Tag& tag) {
    tags_.emplace_back(tag);
    tag_names_and_values_.push_back(tag_pool_.add(tag.name_));
    tag_names_and_values_.push_back(tag_pool_.add(tag.value_));
  }

private:
  std::vector<Tag> tags_;
  std::vector<StatName> tag_names_and_values_;
  std::string tag_extracted_name_;
  StatNamePool tag_pool_;
  std::unique_ptr<StatNameManagedStorage> tag_extracted_stat_name_;
};

template <class BaseClass> class MockStatWithRefcount : public MockMetric<BaseClass> {
public:
  // RefcountInterface
  void incRefCount() override { refcount_helper_.incRefCount(); }
  bool decRefCount() override { return refcount_helper_.decRefCount(); }
  uint32_t use_count() const override { return refcount_helper_.use_count(); }

  RefcountHelper refcount_helper_;
};

class MockCounter : public MockStatWithRefcount<Counter> {
public:
  MockCounter();
  ~MockCounter() override;

  MOCK_METHOD(void, add, (uint64_t amount));
  MOCK_METHOD(void, inc, ());
  MOCK_METHOD(uint64_t, latch, ());
  MOCK_METHOD(void, reset, ());
  MOCK_METHOD(bool, used, (), (const));
  MOCK_METHOD(uint64_t, value, (), (const));

  bool used_;
  uint64_t value_;
  uint64_t latch_;

  // RefcountInterface
  void incRefCount() override { refcount_helper_.incRefCount(); }
  bool decRefCount() override { return refcount_helper_.decRefCount(); }
  uint32_t use_count() const override { return refcount_helper_.use_count(); }

private:
  RefcountHelper refcount_helper_;
};

class MockGauge : public MockStatWithRefcount<Gauge> {
public:
  MockGauge();
  ~MockGauge() override;

  MOCK_METHOD(void, add, (uint64_t amount));
  MOCK_METHOD(void, dec, ());
  MOCK_METHOD(void, inc, ());
  MOCK_METHOD(void, set, (uint64_t value));
  MOCK_METHOD(void, sub, (uint64_t amount));
  MOCK_METHOD(void, mergeImportMode, (ImportMode));
  MOCK_METHOD(bool, used, (), (const));
  MOCK_METHOD(uint64_t, value, (), (const));
  MOCK_METHOD(absl::optional<bool>, cachedShouldImport, (), (const));
  MOCK_METHOD(ImportMode, importMode, (), (const));

  bool used_;
  uint64_t value_;
  ImportMode import_mode_;

  // RefcountInterface
  void incRefCount() override { refcount_helper_.incRefCount(); }
  bool decRefCount() override { return refcount_helper_.decRefCount(); }
  uint32_t use_count() const override { return refcount_helper_.use_count(); }

private:
  RefcountHelper refcount_helper_;
};

class MockHistogram : public MockMetric<Histogram> {
public:
  MockHistogram();
  ~MockHistogram() override;

  MOCK_METHOD(bool, used, (), (const));
  MOCK_METHOD(Histogram::Unit, unit, (), (const));
  MOCK_METHOD(void, recordValue, (uint64_t value));

  // RefcountInterface
  void incRefCount() override { refcount_helper_.incRefCount(); }
  bool decRefCount() override { return refcount_helper_.decRefCount(); }
  uint32_t use_count() const override { return refcount_helper_.use_count(); }

  Unit unit_{Histogram::Unit::Unspecified};
  Store* store_;

private:
  RefcountHelper refcount_helper_;
};

class MockParentHistogram : public MockMetric<ParentHistogram> {
public:
  MockParentHistogram();
  ~MockParentHistogram() override;

  void merge() override {}
  const std::string quantileSummary() const override { return ""; };
  const std::string bucketSummary() const override { return ""; };

  MOCK_METHOD(bool, used, (), (const));
  MOCK_METHOD(Histogram::Unit, unit, (), (const));
  MOCK_METHOD(void, recordValue, (uint64_t value));
  MOCK_METHOD(const HistogramStatistics&, cumulativeStatistics, (), (const));
  MOCK_METHOD(const HistogramStatistics&, intervalStatistics, (), (const));

  // RefcountInterface
  void incRefCount() override { refcount_helper_.incRefCount(); }
  bool decRefCount() override { return refcount_helper_.decRefCount(); }
  uint32_t use_count() const override { return refcount_helper_.use_count(); }

  bool used_;
  Unit unit_{Histogram::Unit::Unspecified};
  Store* store_;
  std::shared_ptr<HistogramStatistics> histogram_stats_ =
      std::make_shared<HistogramStatisticsImpl>();

private:
  RefcountHelper refcount_helper_;
};

class MockMetricSnapshot : public MetricSnapshot {
public:
  MockMetricSnapshot();
  ~MockMetricSnapshot() override;

  MOCK_METHOD(const std::vector<CounterSnapshot>&, counters, ());
  MOCK_METHOD(const std::vector<std::reference_wrapper<const Gauge>>&, gauges, ());
  MOCK_METHOD(const std::vector<std::reference_wrapper<const ParentHistogram>>&, histograms, ());

  std::vector<CounterSnapshot> counters_;
  std::vector<std::reference_wrapper<const Gauge>> gauges_;
  std::vector<std::reference_wrapper<const ParentHistogram>> histograms_;
};

class MockSink : public Sink {
public:
  MockSink();
  ~MockSink() override;

  MOCK_METHOD(void, flush, (MetricSnapshot & snapshot));
  MOCK_METHOD(void, onHistogramComplete, (const Histogram& histogram, uint64_t value));
};

class SymbolTableProvider {
public:
  TestSymbolTable global_symbol_table_;
};

class MockStore : public SymbolTableProvider, public StoreImpl {
public:
  MockStore();
  ~MockStore() override;

  ScopePtr createScope(const std::string& name) override { return ScopePtr{createScope_(name)}; }

  MOCK_METHOD(void, deliverHistogramToSinks, (const Histogram& histogram, uint64_t value));
  MOCK_METHOD(Counter&, counter, (const std::string&));
  MOCK_METHOD(std::vector<CounterSharedPtr>, counters, (), (const));
  MOCK_METHOD(Scope*, createScope_, (const std::string& name));
  MOCK_METHOD(Gauge&, gauge, (const std::string&, Gauge::ImportMode));
  MOCK_METHOD(NullGaugeImpl&, nullGauge, (const std::string&));
  MOCK_METHOD(std::vector<GaugeSharedPtr>, gauges, (), (const));
  MOCK_METHOD(Histogram&, histogram, (const std::string&, Histogram::Unit));
  MOCK_METHOD(std::vector<ParentHistogramSharedPtr>, histograms, (), (const));

  MOCK_METHOD(OptionalCounter, findCounter, (StatName), (const));
  MOCK_METHOD(OptionalGauge, findGauge, (StatName), (const));
  MOCK_METHOD(OptionalHistogram, findHistogram, (StatName), (const));

  Counter& counterFromStatName(StatName name) override {
    return counter(symbol_table_->toString(name));
  }
  Gauge& gaugeFromStatName(StatName name, Gauge::ImportMode import_mode) override {
    return gauge(symbol_table_->toString(name), import_mode);
  }
  Histogram& histogramFromStatName(StatName name, Histogram::Unit unit) override {
    return histogram(symbol_table_->toString(name), unit);
  }

  TestSymbolTable symbol_table_;
  testing::NiceMock<MockCounter> counter_;
  std::vector<std::unique_ptr<MockHistogram>> histograms_;
};

/**
 * With IsolatedStoreImpl it's hard to test timing stats.
 * MockIsolatedStatsStore mocks only deliverHistogramToSinks for better testing.
 */
class MockIsolatedStatsStore : public SymbolTableProvider, public TestUtil::TestStore {
public:
  MockIsolatedStatsStore();
  ~MockIsolatedStatsStore() override;

  MOCK_METHOD(void, deliverHistogramToSinks, (const Histogram& histogram, uint64_t value));

#if 0
  // These overrides are used from production code to populate the store.
  ScopePtr createScope(const std::string& name) override { return store().createScope(name); }
  Counter& counterFromStatName(StatName name) override { return store().counterFromStatName(name); }
  Gauge& gaugeFromStatName(StatName name, Gauge::ImportMode import_mode) override {
    return store().gaugeFromStatName(name, import_mode);
  }
  Histogram& histogramFromStatName(StatName name, Histogram::Unit unit) override {
    return store().histogramFromStatName(name, unit);
  }
  NullGaugeImpl& nullGauge(const std::string& name) override { return store().nullGauge(name); }
  OptionalCounter findCounter(StatName name) const override { return store().findCounter(name); }
  OptionalGauge findGauge(StatName name) const override { return store().findGauge(name); }
  OptionalHistogram findHistogram(StatName name) const override {
    return store().findHistogram(name);
  }
  const SymbolTable& constSymbolTable() const override { return store().constSymbolTable(); }
  SymbolTable& symbolTable() override { return store().symbolTable(); }
  std::vector<CounterSharedPtr> counters() const override { return store().counters(); }
  std::vector<GaugeSharedPtr> gauges() const override { return store().gauges(); }
  std::vector<ParentHistogramSharedPtr> histograms() const override { return store().histograms(); }

  // These overrides are used exclusively from tests to find them.
  Counter& counter(const std::string& name) override { return test_store_.counter(name); }
  Gauge& gauge(const std::string& name, Gauge::ImportMode import_mode) override {
    return test_store_.gauge(name, import_mode);
  }
  Histogram& histogram(const std::string& name, Histogram::Unit unit) override {
    return test_store_.histogram(name, unit);
  }

private:
  Store& store() { return test_store_.store(); }
  const Store& store() const { return test_store_.store(); }

  TestUtil::TestStore test_store_;
#endif
};

class MockStatsMatcher : public StatsMatcher {
public:
  MockStatsMatcher();
  ~MockStatsMatcher() override;
  MOCK_METHOD(bool, rejects, (const std::string& name), (const));
  bool acceptsAll() const override { return accepts_all_; }
  bool rejectsAll() const override { return rejects_all_; }

  bool accepts_all_{false};
  bool rejects_all_{false};
};

} // namespace Stats
} // namespace Envoy
