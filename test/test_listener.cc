#include "test/test_listener.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"

#include "test/test_common/global.h"

namespace Envoy {

void TestListener::OnTestStart(const ::testing::TestInfo& test_info) {
  UNREFERENCED_PARAMETER(test_info);
  // Thread::MainThread::clear();
  // Thread::MainThread::initTestThread();
}

void TestListener::OnTestEnd(const ::testing::TestInfo& test_info) {
  // Check that all singletons have been destroyed.
  std::string active_singletons = Envoy::Test::Globals::describeActiveSingletons();
  RELEASE_ASSERT(active_singletons.empty(),
                 absl::StrCat("FAIL [", test_info.test_suite_name(), ".", test_info.name(),
                              "]: Active singletons exist. Something is leaking. Consider "
                              "commenting out this assert and letting the heap checker run:\n",
                              active_singletons));
  // Thread::MainThread::clearMainThread();
}

} // namespace Envoy
