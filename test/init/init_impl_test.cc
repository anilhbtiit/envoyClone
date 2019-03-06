#include <cstdint>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "init/init_impl.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;

namespace Envoy {
namespace Init {
namespace {

struct MockClient {
  MOCK_CONST_METHOD0(callback, void());
  Receiver receiver_{"test", [this]() { callback(); }};

  testing::internal::TypedExpectation<void()>& expectCallback() {
    return EXPECT_CALL(*this, callback());
  }
};

struct MockTarget {
  MockTarget(absl::string_view name)
      : target_receiver_{name, [this](Caller caller) { initialize(caller); }} {}
  MockTarget(absl::string_view name, Manager& m) : MockTarget(name) { m.add(target_receiver_); }
  MOCK_CONST_METHOD1(initialize, void(Caller));
  TargetReceiver target_receiver_;
  Caller caller_;

  testing::internal::TypedExpectation<void(Caller)>& expectInitialize() {
    return EXPECT_CALL(*this, initialize(_));
  }

  // initialize() will complete immediately
  void expectInitializeImmediate() {
    expectInitialize().WillOnce(Invoke([](Caller caller) { caller(); }));
  }

  // initialize() will save its caller to complete asynchronously
  void expectInitializeAsync() {
    expectInitialize().WillOnce(Invoke([this](Caller caller) { caller_ = caller; }));
  }
};

void expectUninitialized(const Manager& m) { EXPECT_EQ(Manager::State::Uninitialized, m.state()); }
void expectInitializing(const Manager& m) { EXPECT_EQ(Manager::State::Initializing, m.state()); }
void expectInitialized(const Manager& m) { EXPECT_EQ(Manager::State::Initialized, m.state()); }

TEST(ManagerTest, AddImmediateTargetsWhenUninitialized) {
  InSequence s;

  MockClient c;
  ManagerImpl m(c.receiver_, "test");
  expectUninitialized(m);

  MockTarget t1("t1", m);
  t1.expectInitializeImmediate();

  MockTarget t2("t2", m);
  t2.expectInitializeImmediate();

  // initialization should complete immediately
  c.expectCallback();
  m.initialize();
  expectInitialized(m);
}

TEST(ManagerTest, AddAsyncTargetsWhenUninitialized) {
  InSequence s;

  MockClient c;
  ManagerImpl m(c.receiver_, "test");
  expectUninitialized(m);

  MockTarget t1("t1", m);
  t1.expectInitializeAsync();

  MockTarget t2("t2", m);
  t2.expectInitializeAsync();

  // initialization should begin
  m.initialize();
  expectInitializing(m);

  // should still be initializing after first target initializes
  t1.caller_();
  expectInitializing(m);

  // initialization should finish after second target initializes
  c.expectCallback();
  t2.caller_();
  expectInitialized(m);
}

TEST(ManagerTest, AddMixedTargetsWhenUninitialized) {
  InSequence s;

  MockClient c;
  ManagerImpl m(c.receiver_, "test");
  expectUninitialized(m);

  MockTarget t1("t1", m);
  t1.expectInitializeImmediate();

  MockTarget t2("t2", m);
  t2.expectInitializeAsync();

  // initialization should begin, and first target will initialize immediately
  m.initialize();
  expectInitializing(m);

  // initialization should finish after second target initializes
  c.expectCallback();
  t2.caller_();
  expectInitialized(m);
}

TEST(ManagerTest, AddImmediateTargetWhenInitializing) {
  InSequence s;

  MockClient c;
  ManagerImpl m(c.receiver_, "test");
  expectUninitialized(m);

  // need an initial async target so initialization doesn't finish immediately
  MockTarget t1("t1", m);
  t1.expectInitializeAsync();
  m.initialize();
  expectInitializing(m);

  // adding an immediate target shouldn't finish initialization
  MockTarget t2("t2");
  t2.expectInitializeImmediate();
  m.add(t2.target_receiver_);
  expectInitializing(m);

  c.expectCallback();
  t1.caller_();
  expectInitialized(m);
}

TEST(ManagerTest, AddImmediateTargetWhenInitializingEmpty) {
  InSequence s;

  MockClient c;
  ManagerImpl m(c.receiver_, "test");
  expectUninitialized(m);

  // it's legal to start initialization with no targets
  m.initialize();
  expectInitializing(m);

  // adding a target that initializes immediately will complete initialization
  MockTarget t1("t1");
  t1.expectInitializeImmediate();
  c.expectCallback();
  m.add(t1.target_receiver_);
  expectInitialized(m);
}

TEST(ManagerTest, AddAsyncTargetWhenInitializingEmpty) {
  InSequence s;

  MockClient c;
  ManagerImpl m(c.receiver_, "test");
  expectUninitialized(m);

  // it's legal to start initialization with no targets
  m.initialize();
  expectInitializing(m);

  // adding an async target shouldn't finish initialization
  MockTarget t1("t1");
  t1.expectInitializeAsync();
  m.add(t1.target_receiver_);
  expectInitializing(m);

  // initialization should finish after target initializes
  c.expectCallback();
  t1.caller_();
  expectInitialized(m);
}

TEST(ManagerTest, AddWhenInitialized) {
  InSequence s;

  MockClient c;
  ManagerImpl m(c.receiver_, "test");
  expectUninitialized(m);

  // initialize
  MockTarget t1("t1", m);
  t1.expectInitializeImmediate();
  c.expectCallback();
  m.initialize();
  expectInitialized(m);

  MockTarget t2("t2");
  EXPECT_DEATH(m.add(t2.target_receiver_),
               "attempted to add target t2 to initialized init manager test");
}

TEST(ManagerTest, InitializeWhenInitializing) {
  InSequence s;

  MockClient c;
  ManagerImpl m(c.receiver_, "test");
  expectUninitialized(m);

  MockTarget t1("t1", m);
  t1.expectInitializeAsync();

  // initialization should begin
  m.initialize();
  expectInitializing(m);

  EXPECT_DEATH(m.initialize(), "attempted to initialize init manager test twice");
}

TEST(ManagerTest, InitializeWhenInitialized) {
  InSequence s;

  MockClient c;
  ManagerImpl m(c.receiver_, "test");
  expectUninitialized(m);

  // initialize
  MockTarget t1("t1", m);
  t1.expectInitializeImmediate();
  c.expectCallback();
  m.initialize();
  expectInitialized(m);

  EXPECT_DEATH(m.initialize(), "attempted to initialize init manager test twice");
}

TEST(ManagerTest, UnavailableTarget) {
  InSequence s;

  MockClient c;
  ManagerImpl m(c.receiver_, "test");
  expectUninitialized(m);

  MockTarget t1("t1", m);
  t1.target_receiver_.reset();
  t1.expectInitialize().Times(0);

  // initialization should begin and get stuck
  c.expectCallback().Times(0);
  m.initialize();
  expectInitializing(m);
}

TEST(ManagerTest, UnavailableManager) {
  InSequence s;

  MockClient c;
  auto m = new ManagerImpl(c.receiver_, "test");
  expectUninitialized(*m);

  MockTarget t1("t1", *m);
  t1.expectInitializeAsync();

  // initialization should begin
  m->initialize();
  expectInitializing(*m);

  // initialization should get stuck after init manager is destroyed
  delete m;
  c.expectCallback().Times(0);
  t1.caller_();
}

TEST(ManagerTest, UnavailableClient) {
  InSequence s;

  auto c = new MockClient();
  ManagerImpl m(c->receiver_, "test");
  expectUninitialized(m);

  MockTarget t1("t1", m);
  t1.expectInitializeAsync();

  // initialization should begin
  m.initialize();
  expectInitializing(m);

  // initialization should not crash after client is destroyed
  delete c;
  t1.caller_();
}

} // namespace
} // namespace Init
} // namespace Envoy
