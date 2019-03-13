#include "common/safe_init/manager_impl.h"

#include "test/mocks/safe_init/mocks.h"

#include "gtest/gtest.h"

using ::testing::InSequence;
using ::testing::InvokeWithoutArgs;

namespace Envoy {
namespace SafeInit {
namespace {

void expectUninitialized(const Manager& m) { EXPECT_EQ(Manager::State::Uninitialized, m.state()); }
void expectInitializing(const Manager& m) { EXPECT_EQ(Manager::State::Initializing, m.state()); }
void expectInitialized(const Manager& m) { EXPECT_EQ(Manager::State::Initialized, m.state()); }

TEST(ManagerTest, AddImmediateTargetsWhenUninitialized) {
  InSequence s;

  ManagerImpl m("test");
  expectUninitialized(m);

  MockTarget t1("t1");
  m.add(t1);

  MockTarget t2("t2");
  m.add(t2);

  MockWatcher w;

  // initialization should complete immediately
  t1.expectInitializeWillCallReady();
  t2.expectInitializeWillCallReady();
  w.expectReady();
  m.initialize(w);
  expectInitialized(m);
}

TEST(ManagerTest, AddAsyncTargetsWhenUninitialized) {
  InSequence s;

  ManagerImpl m("test");
  expectUninitialized(m);

  MockTarget t1("t1");
  m.add(t1);

  MockTarget t2("t2");
  m.add(t2);

  MockWatcher w;

  // initialization should begin
  t1.expectInitialize();
  t2.expectInitialize();
  m.initialize(w);
  expectInitializing(m);

  // should still be initializing after first target initializes
  t1.ready();
  expectInitializing(m);

  // initialization should finish after second target initializes
  w.expectReady();
  t2.ready();
  expectInitialized(m);
}

TEST(ManagerTest, AddMixedTargetsWhenUninitialized) {
  InSequence s;

  ManagerImpl m("test");
  expectUninitialized(m);

  MockTarget t1("t1");
  m.add(t1);

  MockTarget t2("t2");
  m.add(t2);

  MockWatcher w;

  // initialization should begin, and first target will initialize immediately
  t1.expectInitializeWillCallReady();
  t2.expectInitialize();
  m.initialize(w);
  expectInitializing(m);

  // initialization should finish after second target initializes
  w.expectReady();
  t2.ready();
  expectInitialized(m);
}

TEST(ManagerTest, AddImmediateTargetWhenInitializing) {
  InSequence s;

  ManagerImpl m("test");
  expectUninitialized(m);

  MockTarget t1("t1");
  m.add(t1);

  MockWatcher w;

  // initialization should begin
  t1.expectInitialize();
  m.initialize(w);
  expectInitializing(m);

  // adding an immediate target shouldn't finish initialization
  MockTarget t2("t2");
  t2.expectInitializeWillCallReady();
  m.add(t2);
  expectInitializing(m);

  // initialization should finish after original target initializes
  w.expectReady();
  t1.ready();
  expectInitialized(m);
}

TEST(ManagerTest, AddWhenInitialized) {
  InSequence s;

  ManagerImpl m("test");
  expectUninitialized(m);

  // initializing an empty manager should finish immediately
  MockWatcher w;
  w.expectReady();
  m.initialize(w);
  expectInitialized(m);

  // adding a target should fail
  MockTarget t("t");
  EXPECT_DEATH(m.add(t), "attempted to add target t to initialized init manager test");
}

TEST(ManagerTest, InitializeWhenInitializing) {
  InSequence s;

  ManagerImpl m("test");
  expectUninitialized(m);

  MockTarget t("t");
  m.add(t);

  MockWatcher w;

  // initialization should begin
  t.expectInitialize();
  m.initialize(w);
  expectInitializing(m);

  // re-initializing should fail
  EXPECT_DEATH(m.initialize(w), "attempted to initialize init manager test twice");
}

TEST(ManagerTest, InitializeWhenInitialized) {
  InSequence s;

  ManagerImpl m("test");
  expectUninitialized(m);

  // initializing an empty manager should finish immediately
  MockWatcher w;
  w.expectReady();
  m.initialize(w);
  expectInitialized(m);

  // re-initializing should fail
  EXPECT_DEATH(m.initialize(w), "attempted to initialize init manager test twice");
}

TEST(ManagerTest, UnavailableTarget) {
  InSequence s;

  ManagerImpl m("test");
  expectUninitialized(m);

  // add a target and destroy it
  {
    MockTarget t("t");
    m.add(t);
    t.expectInitialize().Times(0);
  }

  MockWatcher w;

  // initialization should complete despite the destroyed target
  w.expectReady();
  m.initialize(w);
  expectInitialized(m);
}

TEST(ManagerTest, UnavailableManager) {
  InSequence s;

  MockTarget t("t");
  MockWatcher w;

  {
    ManagerImpl m("test");
    expectUninitialized(m);

    m.add(t);

    // initialization should begin before destroying the manager
    t.expectInitialize();
    m.initialize(w);
    expectInitializing(m);
  }

  // the watcher should not be notified when the target is initialized
  w.expectReady().Times(0);
  t.ready();
}

TEST(ManagerTest, UnavailableWatcher) {
  InSequence s;

  ManagerImpl m("test");
  expectUninitialized(m);

  MockTarget t("t");
  m.add(t);

  {
    MockWatcher w;

    // initialization should begin before destroying the watcher
    t.expectInitialize();
    m.initialize(w);
    expectInitializing(m);

    w.expectReady().Times(0);
  }

  // initialization should finish without notifying the watcher
  t.ready();
}

} // namespace
} // namespace SafeInit
} // namespace Envoy
