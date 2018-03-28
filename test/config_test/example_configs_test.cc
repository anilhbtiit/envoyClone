#include "test/config_test/config_test.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
TEST(ExampleConfigsTest, All) {
  TestEnvironment::exec(
      {TestEnvironment::runfilesPath("test/config_test/example_configs_test_setup.sh")});

  // Change working directory, otherwise we won't be able to read files using relative paths.
  char cwd[PATH_MAX];
  const std::string& directory = TestEnvironment::temporaryDirectory() + "/test/config_test";
  RELEASE_ASSERT(::getcwd(cwd, PATH_MAX) != nullptr);
  RELEASE_ASSERT(::chdir(directory.c_str()) == 0);

  EXPECT_EQ(37UL, ConfigTest::run(directory));
  ConfigTest::testMerge();

  // Return to the original working directory, otherwise "bazel.coverage" breaks (...but why?).
  RELEASE_ASSERT(::chdir(cwd) == 0);
}
} // namespace Envoy
