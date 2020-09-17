#include "envoy/extensions/watchdog/abort_action/v3alpha/abort_action.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/guarddog_config.h"

#include "extensions/watchdog/abort_action/config.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace AbortAction {
namespace {

TEST(AbortActionFactoryTest, CanCreateAction) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::GuardDogActionFactory>::getFactory(
          "envoy.watchdog.abort_action");
  ASSERT_NE(factory, nullptr);

  // Create config and mock context
  envoy::config::bootstrap::v3::Watchdog::WatchdogAction config;
  TestUtility::loadFromJson(
      R"EOF(
        {
          "config": {
            "name": "envoy.watchdog.abort_action",
            "typed_config": {
	      "@type": "type.googleapis.com/udpa.type.v1.TypedStruct",
	      "type_url": "type.googleapis.com/envoy.extensions.watchdog.abort_action.v3alpha.AbortActionConfig",
	      "value": {
		"wait_duration": "2s",
	      }
            }
          },
        }
      )EOF",
      config);

  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::Configuration::GuardDogActionFactoryContext context{*api, dispatcher};

  EXPECT_NE(factory->createGuardDogActionFromProto(config, context), nullptr);
}

} // namespace
} // namespace AbortAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
