#include <string>

#include "server/config/network/mongo_proxy.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "api/filter/network/mongo_proxy.pb.h"

using testing::NiceMock;
using testing::_;

namespace Envoy {
namespace Server {
namespace Configuration {

TEST(MongoFilterConfigTest, CorrectConfigurationNoFaults) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "access_log" : "path/to/access/log"
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;
  NetworkFilterFactoryCb cb = factory.createFilterFactory(*json_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

TEST(MongoFilterConfigTest, CorrectProtoConfigurationNoFaults) {
  envoy::api::v2::filter::network::MongoProxy config{};

  config.set_access_log("path/to/access/log");
  config.set_stat_prefix("my_stat_prefix");

  NiceMock<MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;
  NetworkFilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

void handleInvalidConfiguration(const std::string& json_string) {
  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;

  EXPECT_THROW(factory.createFilterFactory(*json_config, context), Json::Exception);
}

    void handleInvalidProto(const envoy::api::v2::filter::network::MongoProxy& config) {
      NiceMock<MockFactoryContext> context;
      MongoProxyFilterConfigFactory factory;

      EXPECT_THROW(factory.createFilterFactoryFromProto(config, context), MissingFieldException);
    }

TEST(MongoFilterConfigTest, InvalidExtraProperty) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "access_log" : "path/to/access/log",
    "test" : "a"
  }
  )EOF";

  handleInvalidConfiguration(json_string);
}

TEST(MongoFilterConfigTest, EmptyConfig) { handleInvalidConfiguration("{}"); }

TEST(MongoFilterConfigTest, EmptyProto) {
envoy::api::v2::filter::network::MongoProxy config{};
      handleInvalidProto(config);
    }

TEST(MongoFilterConfigTest, InvalidFaultsEmptyConfig) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "fault" : {}
  }
  )EOF";

  handleInvalidConfiguration(json_string);
}

TEST(MongoFilterConfigTest, InvalidFaultsMissingPercentage) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "fault" : {
      "fixed_delay": {
        "duration_ms": 1
      }
    }
  }
  )EOF";

  handleInvalidConfiguration(json_string);
}

TEST(MongoFilterConfigTest, InvalidFaultsMissingMs) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "fault" : {
      "fixed_delay": {
        "delay_percent": 1
      }
    }
  }
  )EOF";

  handleInvalidConfiguration(json_string);
}

    TEST(MongoFilterConfigTest, InvalidFaultsMissingDurationInProto) {
      envoy::api::v2::filter::network::MongoProxy config{};
      config.set_stat_prefix("my_stat_prefix");
      config.mutable_delay()->set_percent(50);
      handleInvalidProto(config);
    }

    TEST(MongoFilterConfigTest, InvalidFaultsNegativeMs) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "fault" : {
      "fixed_delay": {
        "percent": 1,
        "duration_ms": -1
      }
    }
  }
  )EOF";

  handleInvalidConfiguration(json_string);
}

TEST(MongoFilterConfigTest, InvalidFaultsDelayPercent) {
  {
    std::string json_string = R"EOF(
    {
      "stat_prefix": "my_stat_prefix",
      "fault" : {
        "fixed_delay": {
          "percent": 101,
          "duration_ms": 1
        }
      }
    }
    )EOF";

    handleInvalidConfiguration(json_string);
  }

  {
    std::string json_string = R"EOF(
    {
      "stat_prefix": "my_stat_prefix",
      "fault" : {
        "fixed_delay": {
          "percent": -1,
          "duration_ms": 1
        }
      }
    }
    )EOF";

    handleInvalidConfiguration(json_string);
  }
}

TEST(MongoFilterConfigTest, InvalidFaultsType) {
  {
    std::string json_string = R"EOF(
    {
      "stat_prefix": "my_stat_prefix",
      "fault" : {
        "fixed_delay": {
          "percent": "df",
          "duration_ms": 1
        }
      }
    }
    )EOF";

    handleInvalidConfiguration(json_string);
  }

  {
    std::string json_string = R"EOF(
    {
      "stat_prefix": "my_stat_prefix",
      "fault" : {
        "fixed_delay": {
          "percent": 3,
          "duration_ms": "ab"
        }
      }
    }
    )EOF";

    handleInvalidConfiguration(json_string);
  }

  {
    std::string json_string = R"EOF(
    {
      "stat_prefix": "my_stat_prefix",
      "fault" : {
        "fixed_delay": {
          "percent": 3,
          "duration_ms": "0"
        }
      }
    }
    )EOF";

    handleInvalidConfiguration(json_string);
  }
}

TEST(MongoFilterConfigTest, CorrectFaultConfiguration) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "fault" : {
      "fixed_delay": {
        "percent": 1,
        "duration_ms": 1
      }
    }
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;
  NetworkFilterFactoryCb cb = factory.createFilterFactory(*json_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

    TEST(MongoFilterConfigTest, CorrectFaultConfigurationInProto) {
      envoy::api::v2::filter::network::MongoProxy config{};
      config.set_stat_prefix("my_stat_prefix");
      config.mutable_delay()->set_percent(50);
      config.mutable_delay()->mutable_fixed_delay()->set_seconds(500);

      NiceMock<MockFactoryContext> context;
      MongoProxyFilterConfigFactory factory;
      NetworkFilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
      Network::MockConnection connection;
      EXPECT_CALL(connection, addFilter(_));
      cb(connection);
    }

} // namespace Configuration
} // namespace Server
} // namespace Envoy
