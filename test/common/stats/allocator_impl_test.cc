#include <cmath>
#include <string>

#include "source/common/stats/allocator_impl.h"

#include "test/test_common/logging.h"
#include "test/test_common/thread_factory_for_test.h"

#include "absl/synchronization/notification.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class AllocatorImplTest : public testing::Test {
protected:
  AllocatorImplTest() : alloc_(symbol_table_), pool_(symbol_table_) {}
  ~AllocatorImplTest() override { clearStorage(); }

  StatNameStorage makeStatStorage(absl::string_view name) {
    return StatNameStorage(name, symbol_table_);
  }

  StatName makeStat(absl::string_view name) { return pool_.add(name); }

  void clearStorage() {
    pool_.clear();
    // If stats have been marked for deletion, they are not cleared until the
    // destructor of alloc_ is called, and hence the symbol_table_.numSymbols()
    // will be greater than zero at this point.
    if (!are_stats_marked_for_deletion_) {
      EXPECT_EQ(0, symbol_table_.numSymbols());
    }
  }

  SymbolTableImpl symbol_table_;
  AllocatorImpl alloc_;
  StatNamePool pool_;
  bool are_stats_marked_for_deletion_ = false;
};

// Allocate 2 counters of the same name, and you'll get the same object.
TEST_F(AllocatorImplTest, CountersWithSameName) {
  StatName counter_name = makeStat("counter.name");
  CounterSharedPtr c1 = alloc_.makeCounter(counter_name, StatName(), {});
  EXPECT_EQ(1, c1->use_count());
  CounterSharedPtr c2 = alloc_.makeCounter(counter_name, StatName(), {});
  EXPECT_EQ(2, c1->use_count());
  EXPECT_EQ(2, c2->use_count());
  EXPECT_EQ(c1.get(), c2.get());
  EXPECT_FALSE(c1->used());
  EXPECT_FALSE(c2->used());
  c1->inc();
  EXPECT_TRUE(c1->used());
  EXPECT_TRUE(c2->used());
  c2->inc();
  EXPECT_EQ(2, c1->value());
  EXPECT_EQ(2, c2->value());
}

TEST_F(AllocatorImplTest, GaugesWithSameName) {
  StatName gauge_name = makeStat("gauges.name");
  GaugeSharedPtr g1 = alloc_.makeGauge(gauge_name, StatName(), {}, Gauge::ImportMode::Accumulate);
  EXPECT_EQ(1, g1->use_count());
  GaugeSharedPtr g2 = alloc_.makeGauge(gauge_name, StatName(), {}, Gauge::ImportMode::Accumulate);
  EXPECT_EQ(2, g1->use_count());
  EXPECT_EQ(2, g2->use_count());
  EXPECT_EQ(g1.get(), g2.get());
  EXPECT_FALSE(g1->used());
  EXPECT_FALSE(g2->used());
  g1->inc();
  EXPECT_TRUE(g1->used());
  EXPECT_TRUE(g2->used());
  EXPECT_EQ(1, g1->value());
  EXPECT_EQ(1, g2->value());
  g2->dec();
  EXPECT_EQ(0, g1->value());
  EXPECT_EQ(0, g2->value());
}

// Test for a race-condition where we may decrement the ref-count of a stat to
// zero at the same time as we are allocating another instance of that
// stat. This test reproduces that race organically by having a 12 threads each
// iterate 10k times.
TEST_F(AllocatorImplTest, RefCountDecAllocRaceOrganic) {
  StatName counter_name = makeStat("counter.name");
  StatName gauge_name = makeStat("gauge.name");
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();

  const uint32_t num_threads = 12;
  const uint32_t iters = 10000;
  std::vector<Thread::ThreadPtr> threads;
  absl::Notification go;
  for (uint32_t i = 0; i < num_threads; ++i) {
    threads.push_back(thread_factory.createThread([&]() {
      go.WaitForNotification();
      for (uint32_t i = 0; i < iters; ++i) {
        alloc_.makeCounter(counter_name, StatName(), {});
        alloc_.makeGauge(gauge_name, StatName(), {}, Gauge::ImportMode::NeverImport);
      }
    }));
  }
  go.Notify();
  for (uint32_t i = 0; i < num_threads; ++i) {
    threads[i]->join();
  }
}

// Tests the same scenario as RefCountDecAllocRaceOrganic, but using just two
// threads and the ThreadSynchronizer, in one iteration. Note that if the code
// has the bug in it, this test fails fast as expected. However, if the bug is
// fixed, the allocator's mutex will cause the second thread to block in
// makeCounter() until the first thread finishes destructing the object. Thus
// the test gives thread2 5 seconds to complete before releasing thread 1 to
// complete its destruction of the counter.
TEST_F(AllocatorImplTest, RefCountDecAllocRaceSynchronized) {
  StatName counter_name = makeStat("counter.name");
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();
  alloc_.sync().enable();
  alloc_.sync().waitOn(AllocatorImpl::DecrementToZeroSyncPoint);
  Thread::ThreadPtr thread = thread_factory.createThread([&]() {
    CounterSharedPtr counter = alloc_.makeCounter(counter_name, StatName(), {});
    counter->inc();
    counter->reset(); // Blocks in thread synchronizer waiting on DecrementToZeroSyncPoint
  });

  alloc_.sync().barrierOn(AllocatorImpl::DecrementToZeroSyncPoint);
  EXPECT_TRUE(alloc_.isMutexLockedForTest());
  alloc_.sync().signal(AllocatorImpl::DecrementToZeroSyncPoint);
  thread->join();
  EXPECT_FALSE(alloc_.isMutexLockedForTest());
}

TEST_F(AllocatorImplTest, ForEachCounter) {
  StatNameHashSet stat_names;
  std::vector<CounterSharedPtr> counters;

  const size_t num_stats = 11;

  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("counter.", idx));
    stat_names.insert(stat_name);
    counters.emplace_back(alloc_.makeCounter(stat_name, StatName(), {}));
  }

  size_t num_counters = 0;
  size_t num_iterations = 0;
  alloc_.forEachCounter([&num_counters](std::size_t size) { num_counters = size; },
                        [&num_iterations, &stat_names](Stats::Counter& counter) {
                          EXPECT_EQ(stat_names.count(counter.statName()), 1);
                          ++num_iterations;
                        });
  EXPECT_EQ(num_counters, 11);
  EXPECT_EQ(num_iterations, 11);

  // Reject a stat and remove it from "scope".
  StatName rejected_stat_name = counters[4]->statName();
  alloc_.markCounterForDeletion(counters[4]);
  are_stats_marked_for_deletion_ = true;
  // Save a local reference to rejected stat.
  Counter& rejected_counter = *counters[4];
  counters.erase(counters.begin() + 4);

  // Verify that the rejected stat does not show up during iteration.
  num_iterations = 0;
  num_counters = 0;
  alloc_.forEachCounter([&num_counters](std::size_t size) { num_counters = size; },
                        [&num_iterations, &rejected_stat_name](Stats::Counter& counter) {
                          EXPECT_THAT(counter.statName(), ::testing::Ne(rejected_stat_name));
                          ++num_iterations;
                        });
  EXPECT_EQ(num_iterations, 10);
  EXPECT_EQ(num_counters, 10);

  // Verify that we can access the local reference without a crash.
  rejected_counter.inc();

  // Erase all stats.
  counters.clear();
  num_iterations = 0;
  alloc_.forEachCounter([&num_counters](std::size_t size) { num_counters = size; },
                        [&num_iterations](Stats::Counter&) { ++num_iterations; });
  EXPECT_EQ(num_counters, 0);
  EXPECT_EQ(num_iterations, 0);
}

TEST_F(AllocatorImplTest, ForEachGauge) {
  StatNameHashSet stat_names;
  std::vector<GaugeSharedPtr> gauges;

  const size_t num_stats = 11;

  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("gauge.", idx));
    stat_names.insert(stat_name);
    gauges.emplace_back(alloc_.makeGauge(stat_name, StatName(), {}, Gauge::ImportMode::Accumulate));
  }

  size_t num_gauges = 0;
  size_t num_iterations = 0;
  alloc_.forEachGauge([&num_gauges](std::size_t size) { num_gauges = size; },
                      [&num_iterations, &stat_names](Stats::Gauge& gauge) {
                        EXPECT_EQ(stat_names.count(gauge.statName()), 1);
                        ++num_iterations;
                      });
  EXPECT_EQ(num_gauges, 11);
  EXPECT_EQ(num_iterations, 11);

  // Reject a stat and remove it from "scope".
  StatName rejected_stat_name = gauges[3]->statName();
  alloc_.markGaugeForDeletion(gauges[3]);
  are_stats_marked_for_deletion_ = true;
  // Save a local reference to rejected stat.
  Gauge& rejected_gauge = *gauges[3];
  gauges.erase(gauges.begin() + 3);

  // Verify that the rejected stat does not show up during iteration.
  num_iterations = 0;
  num_gauges = 0;
  alloc_.forEachGauge([&num_gauges](std::size_t size) { num_gauges = size; },
                      [&num_iterations, &rejected_stat_name](Stats::Gauge& gauge) {
                        EXPECT_THAT(gauge.statName(), ::testing::Ne(rejected_stat_name));
                        ++num_iterations;
                      });
  EXPECT_EQ(num_iterations, 10);
  EXPECT_EQ(num_gauges, 10);

  // Verify that we can access the local reference without a crash.
  rejected_gauge.inc();

  // Erase all stats.
  gauges.clear();
  num_iterations = 0;
  alloc_.forEachGauge([&num_gauges](std::size_t size) { num_gauges = size; },
                      [&num_iterations](Stats::Gauge&) { ++num_iterations; });
  EXPECT_EQ(num_gauges, 0);
  EXPECT_EQ(num_iterations, 0);
}

TEST_F(AllocatorImplTest, ForEachTextReadout) {
  StatNameHashSet stat_names;
  std::vector<TextReadoutSharedPtr> text_readouts;

  const size_t num_stats = 11;

  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("text_readout.", idx));
    stat_names.insert(stat_name);
    text_readouts.emplace_back(alloc_.makeTextReadout(stat_name, StatName(), {}));
  }

  size_t num_text_readouts = 0;
  size_t num_iterations = 0;
  alloc_.forEachTextReadout([&num_text_readouts](std::size_t size) { num_text_readouts = size; },
                            [&num_iterations, &stat_names](Stats::TextReadout& text_readout) {
                              EXPECT_EQ(stat_names.count(text_readout.statName()), 1);
                              ++num_iterations;
                            });
  EXPECT_EQ(num_text_readouts, 11);
  EXPECT_EQ(num_iterations, 11);

  // Reject a stat and remove it from "scope".
  StatName rejected_stat_name = text_readouts[4]->statName();
  alloc_.markTextReadoutForDeletion(text_readouts[4]);
  are_stats_marked_for_deletion_ = true;
  // Save a local reference to rejected stat.
  TextReadout& rejected_text_readout = *text_readouts[4];
  text_readouts.erase(text_readouts.begin() + 4);

  // Verify that the rejected stat does not show up during iteration.
  num_iterations = 0;
  num_text_readouts = 0;
  alloc_.forEachTextReadout(
      [&num_text_readouts](std::size_t size) { num_text_readouts = size; },
      [&num_iterations, &rejected_stat_name](Stats::TextReadout& text_readout) {
        EXPECT_THAT(text_readout.statName(), ::testing::Ne(rejected_stat_name));
        ++num_iterations;
      });
  EXPECT_EQ(num_iterations, 10);
  EXPECT_EQ(num_text_readouts, 10);

  // Verify that we can access the local reference without a crash.
  rejected_text_readout.set("no crash");

  // Erase all stats.
  text_readouts.clear();
  num_iterations = 0;
  alloc_.forEachTextReadout([&num_text_readouts](std::size_t size) { num_text_readouts = size; },
                            [&num_iterations](Stats::TextReadout&) { ++num_iterations; });
  EXPECT_EQ(num_text_readouts, 0);
  EXPECT_EQ(num_iterations, 0);
}

// Verify that we don't crash if a nullptr is passed in for the size lambda for
// the for each stat methods.
TEST_F(AllocatorImplTest, ForEachWithNullSizeLambda) {
  std::vector<CounterSharedPtr> counters;
  std::vector<TextReadoutSharedPtr> text_readouts;
  std::vector<GaugeSharedPtr> gauges;

  const size_t num_stats = 3;

  // For each counter.
  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("counter.", idx));
    counters.emplace_back(alloc_.makeCounter(stat_name, StatName(), {}));
  }
  size_t num_iterations = 0;
  alloc_.forEachCounter(nullptr, [&num_iterations](Stats::Counter& counter) {
    (void)counter;
    ++num_iterations;
  });
  EXPECT_EQ(num_iterations, num_stats);

  // For each gauge.
  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("gauge.", idx));
    gauges.emplace_back(alloc_.makeGauge(stat_name, StatName(), {}, Gauge::ImportMode::Accumulate));
  }
  num_iterations = 0;
  alloc_.forEachGauge(nullptr, [&num_iterations](Stats::Gauge& gauge) {
    (void)gauge;
    ++num_iterations;
  });
  EXPECT_EQ(num_iterations, num_stats);

  // For each text readout.
  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("text_readout.", idx));
    text_readouts.emplace_back(alloc_.makeTextReadout(stat_name, StatName(), {}));
  }
  num_iterations = 0;
  alloc_.forEachTextReadout(nullptr, [&num_iterations](Stats::TextReadout& text_readout) {
    (void)text_readout;
    ++num_iterations;
  });
  EXPECT_EQ(num_iterations, num_stats);
}

// Currently, if we ask for a stat from the  Allocator that has already been
// marked for deletion (i.e. rejected) we get a new stat with the same name.
// This test documents this behavior.
TEST_F(AllocatorImplTest, AskForDeletedStat) {
  const size_t num_stats = 10;
  are_stats_marked_for_deletion_ = true;

  std::vector<CounterSharedPtr> counters;
  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("counter.", idx));
    counters.emplace_back(alloc_.makeCounter(stat_name, StatName(), {}));
  }
  // Reject a stat and remove it from "scope".
  StatName const rejected_counter_name = counters[4]->statName();
  alloc_.markCounterForDeletion(counters[4]);
  // Save a local reference to rejected stat.
  Counter& rejected_counter = *counters[4];
  counters.erase(counters.begin() + 4);

  rejected_counter.inc();
  rejected_counter.inc();

  // Make the deleted stat again.
  CounterSharedPtr deleted_counter = alloc_.makeCounter(rejected_counter_name, StatName(), {});

  EXPECT_EQ(deleted_counter->value(), 0);
  EXPECT_EQ(rejected_counter.value(), 2);

  std::vector<GaugeSharedPtr> gauges;
  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("gauge.", idx));
    gauges.emplace_back(alloc_.makeGauge(stat_name, StatName(), {}, Gauge::ImportMode::Accumulate));
  }
  // Reject a stat and remove it from "scope".
  StatName const rejected_gauge_name = gauges[4]->statName();
  alloc_.markGaugeForDeletion(gauges[4]);
  // Save a local reference to rejected stat.
  Gauge& rejected_gauge = *gauges[4];
  gauges.erase(gauges.begin() + 4);

  rejected_gauge.set(10);

  // Make the deleted stat again.
  GaugeSharedPtr deleted_gauge =
      alloc_.makeGauge(rejected_gauge_name, StatName(), {}, Gauge::ImportMode::Accumulate);

  EXPECT_EQ(deleted_gauge->value(), 0);
  EXPECT_EQ(rejected_gauge.value(), 10);

  std::vector<TextReadoutSharedPtr> text_readouts;
  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("text_readout.", idx));
    text_readouts.emplace_back(alloc_.makeTextReadout(stat_name, StatName(), {}));
  }
  // Reject a stat and remove it from "scope".
  StatName const rejected_text_readout_name = text_readouts[4]->statName();
  alloc_.markTextReadoutForDeletion(text_readouts[4]);
  // Save a local reference to rejected stat.
  TextReadout& rejected_text_readout = *text_readouts[4];
  text_readouts.erase(text_readouts.begin() + 4);

  rejected_text_readout.set("deleted value");

  // Make the deleted stat again.
  TextReadoutSharedPtr deleted_text_readout =
      alloc_.makeTextReadout(rejected_text_readout_name, StatName(), {});

  EXPECT_EQ(deleted_text_readout->value(), "");
  EXPECT_EQ(rejected_text_readout.value(), "deleted value");
}

} // namespace
} // namespace Stats
} // namespace Envoy
