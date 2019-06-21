#pragma once

#include "envoy/stats/stats.h"

#include "common/stats/metric_impl.h"

namespace Envoy {
namespace Stats {

/**
 * Null gauge implementation.
 * No-ops on all calls and requires no underlying metric or data.
 */
class NullGaugeImpl : public Gauge, NullMetricImpl {
public:
  explicit NullGaugeImpl(SymbolTable& symbol_table) : NullMetricImpl(symbol_table) {}
  ~NullGaugeImpl() override {
    // MetricImpl must be explicitly cleared() before destruction, otherwise it
    // will not be able to access the SymbolTable& to free the symbols. An RAII
    // alternative would be to store the SymbolTable reference in the
    // MetricImpl, costing 8 bytes per stat.
    MetricImpl::clear();
  }

  //void incRefCount() override { ++ref_count_; }
  //void free() override { if (--ref_count_ == 0) { delete this; } }
  //uint32_t use_count() const override { return ref_count_; }
  void add(uint64_t) override {}
  void inc() override {}
  void dec() override {}
  void set(uint64_t) override {}
  void sub(uint64_t) override {}
  uint64_t value() const override { return 0; }
  ImportMode importMode() const override { return ImportMode::NeverImport; }
  void mergeImportMode(ImportMode /* import_mode */) override {}

 private:
  std::atomic<uint32_t> ref_count_{0};
};

} // namespace Stats
} // namespace Envoy
