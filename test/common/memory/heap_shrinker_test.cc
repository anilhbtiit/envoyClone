#include "common/event/dispatcher_impl.h"
#include "common/memory/heap_shrinker.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_base.h"

#include "gmock/gmock.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Memory {
namespace {

class HeapShrinkerTest : public TestBase {
protected:
  HeapShrinkerTest()
      : api_(Api::createApiForTest(stats_, time_system_)), dispatcher_(*api_, time_system_) {}

  void step() {
    time_system_.sleep(std::chrono::milliseconds(10000));
    dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  }

  Stats::IsolatedStoreImpl stats_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherImpl dispatcher_;
  NiceMock<Server::MockOverloadManager> overload_manager_;
  Event::TimerCb timer_cb_;
};

TEST_F(HeapShrinkerTest, DoNotShrinkWhenNotConfigured) {
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(overload_manager_, registerForAction(_, _, _)).WillOnce(Return(false));
  EXPECT_CALL(dispatcher, createTimer_(_)).Times(0);
  HeapShrinker h(dispatcher, overload_manager_, stats_);
}

TEST_F(HeapShrinkerTest, ShrinkWhenTriggered) {
  Server::OverloadActionCb action_cb;
  EXPECT_CALL(overload_manager_, registerForAction(_, _, _))
      .WillOnce(Invoke([&](const std::string&, Event::Dispatcher&, Server::OverloadActionCb cb) {
        action_cb = cb;
        return true;
      }));

  HeapShrinker h(dispatcher_, overload_manager_, stats_);

  Stats::Counter& shrink_count =
      stats_.counter("overload.envoy.overload_actions.shrink_heap.shrink_count");
  action_cb(Server::OverloadActionState::Active);
  step();
  EXPECT_EQ(1, shrink_count.value());
  step();
  EXPECT_EQ(2, shrink_count.value());

  action_cb(Server::OverloadActionState::Inactive);
  step();
  step();
  EXPECT_EQ(2, shrink_count.value());
}

} // namespace
} // namespace Memory
} // namespace Envoy
