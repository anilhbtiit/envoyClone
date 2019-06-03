#include "common/config/runtime_utility.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

TEST(RuntimeUtility, TranslateEmpty) {
  envoy::config::bootstrap::v2::LayeredRuntime layered_runtime_config;
  translateRuntime({}, layered_runtime_config);
  envoy::config::bootstrap::v2::LayeredRuntime expected_runtime_config;
  expected_runtime_config.add_layers()->mutable_static_layer();
  expected_runtime_config.add_layers()->mutable_admin_layer();
  EXPECT_THAT(layered_runtime_config, ProtoEq(expected_runtime_config));
}

TEST(RuntimeUtility, TranslateSubdirOnly) {
  envoy::config::bootstrap::v2::Runtime runtime_config;
  runtime_config.set_symlink_root("foo");
  runtime_config.set_subdirectory("bar");
  envoy::config::bootstrap::v2::LayeredRuntime layered_runtime_config;
  translateRuntime(runtime_config, layered_runtime_config);
  envoy::config::bootstrap::v2::LayeredRuntime expected_runtime_config;
  expected_runtime_config.add_layers()->mutable_static_layer();
  auto* layer = expected_runtime_config.add_layers();
  layer->set_name("root");
  layer->mutable_disk_layer()->set_symlink_root("foo/bar");
  expected_runtime_config.add_layers()->mutable_admin_layer();
  EXPECT_THAT(layered_runtime_config, ProtoEq(expected_runtime_config));
}

TEST(RuntimeUtility, TranslateSubdirOverride) {
  envoy::config::bootstrap::v2::Runtime runtime_config;
  runtime_config.set_symlink_root("foo");
  runtime_config.set_subdirectory("bar");
  runtime_config.set_override_subdirectory("baz");
  envoy::config::bootstrap::v2::LayeredRuntime layered_runtime_config;
  translateRuntime(runtime_config, layered_runtime_config);
  envoy::config::bootstrap::v2::LayeredRuntime expected_runtime_config;
  expected_runtime_config.add_layers()->mutable_static_layer();
  {
    auto* layer = expected_runtime_config.add_layers();
    layer->set_name("root");
    layer->mutable_disk_layer()->set_symlink_root("foo/bar");
  }
  {
    auto* layer = expected_runtime_config.add_layers();
    layer->set_name("override");
    layer->mutable_disk_layer()->set_symlink_root("foo/baz");
    layer->mutable_disk_layer()->set_append_service_cluster(true);
  }
  expected_runtime_config.add_layers()->mutable_admin_layer();
  EXPECT_THAT(layered_runtime_config, ProtoEq(expected_runtime_config));
}

} // namespace
} // namespace Config
} // namespace Envoy
