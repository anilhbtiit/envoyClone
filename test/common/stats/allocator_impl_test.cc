#include <string>

#include "common/stats/allocator_impl.h"
#include "common/stats/symbol_table_creator.h"

#include "test/test_common/logging.h"
#include "test/test_common/thread_factory_for_test.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class AllocatorImplTest : public testing::Test {
protected:
  AllocatorImplTest()
      : symbol_table_(SymbolTableCreator::makeSymbolTable()), alloc_(*symbol_table_),
        pool_(*symbol_table_) {}
  ~AllocatorImplTest() override { clearStorage(); }

  StatNameStorage makeStatStorage(absl::string_view name) {
    return StatNameStorage(name, *symbol_table_);
  }

  StatName makeStat(absl::string_view name) { return pool_.add(name); }

  void clearStorage() {
    pool_.clear();
    EXPECT_EQ(0, symbol_table_->numSymbols());
  }

  SymbolTablePtr symbol_table_;
  AllocatorImpl alloc_;
  StatNamePool pool_;
};

// Allocate 2 counters of the same name, and you'll get the same object.
TEST_F(AllocatorImplTest, CountersWithSameName) {
  StatName counter_name = makeStat("counter.name");
  CounterSharedPtr c1 = alloc_.makeCounter(counter_name, "", std::vector<Tag>());
  EXPECT_EQ(1, c1->use_count());
  CounterSharedPtr c2 = alloc_.makeCounter(counter_name, "", std::vector<Tag>());
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
  GaugeSharedPtr g1 =
      alloc_.makeGauge(gauge_name, "", std::vector<Tag>(), Gauge::ImportMode::Accumulate);
  EXPECT_EQ(1, g1->use_count());
  GaugeSharedPtr g2 =
      alloc_.makeGauge(gauge_name, "", std::vector<Tag>(), Gauge::ImportMode::Accumulate);
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
        alloc_.makeCounter(counter_name, "", std::vector<Tag>());
        alloc_.makeGauge(gauge_name, "", std::vector<Tag>(), Gauge::ImportMode::NeverImport);
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
  absl::Notification alloc_done;
  Thread::ThreadPtr thread1 = thread_factory.createThread([&]() {
    CounterSharedPtr counter1 = alloc_.makeCounter(counter_name, "", std::vector<Tag>());
    alloc_done.Notify();
    counter1->inc();
    counter1->reset(); // Blocks in thread synchronizer waiting on DecrementToZeroSyncPoint
  });

  alloc_done.WaitForNotification();

  absl::Mutex counter2_created_mutex;
  bool counter2_created = false;

  // counter1 has now been allocated in the thread, and is now in the middle of
  // destructing it.
  Thread::ThreadPtr thread2 = thread_factory.createThread([&]() {
    CounterSharedPtr counter2 = alloc_.makeCounter(counter_name, "", std::vector<Tag>());
    {
      absl::MutexLock lock(&counter2_created_mutex);
      counter2_created = true;
    }
    counter2->inc();

    // We test for a value of 1 here to show that the first instance of the
    // counter was destructed prior to the second instance being created, and
    // thus starts again from zero.
    EXPECT_EQ(1, counter2->value());
  });

  {
    absl::MutexLock lock(&counter2_created_mutex);
    counter2_created_mutex.AwaitWithTimeout(absl::Condition(&counter2_created), absl::Seconds(5));
    EXPECT_FALSE(counter2_created);
  }
  alloc_.sync().signal(AllocatorImpl::DecrementToZeroSyncPoint);

  thread1->join();
  thread2->join();
}

} // namespace
} // namespace Stats
} // namespace Envoy
