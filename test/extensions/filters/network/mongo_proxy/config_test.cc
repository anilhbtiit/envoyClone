#include "envoy/config/filter/network/mongo_proxy/v2/mongo_proxy.pb.validate.h"

#include "extensions/filters/network/mongo_proxy/config.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/test_base.h"

#include "gmock/gmock.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

using MongoFilterConfigTest = TestBase;

TEST_F(MongoFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(MongoProxyFilterConfigFactory().createFilterFactoryFromProto(
                   envoy::config::filter::network::mongo_proxy::v2::MongoProxy(), context),
               ProtoValidationException);
}

TEST_F(MongoFilterConfigTest, CorrectConfigurationNoFaults) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "access_log" : "path/to/access/log"
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactory(*json_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

TEST_F(MongoFilterConfigTest, ValidProtoConfigurationNoFaults) {
  envoy::config::filter::network::mongo_proxy::v2::MongoProxy config{};

  config.set_access_log("path/to/access/log");
  config.set_stat_prefix("my_stat_prefix");

  NiceMock<Server::Configuration::MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

TEST_F(MongoFilterConfigTest, MongoFilterWithEmptyProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;
  envoy::config::filter::network::mongo_proxy::v2::MongoProxy config =
      *dynamic_cast<envoy::config::filter::network::mongo_proxy::v2::MongoProxy*>(
          factory.createEmptyConfigProto().get());
  config.set_access_log("path/to/access/log");
  config.set_stat_prefix("my_stat_prefix");

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

void handleInvalidConfiguration(const std::string& json_string) {
  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;

  EXPECT_THROW(factory.createFilterFactory(*json_config, context), Json::Exception);
}

TEST_F(MongoFilterConfigTest, InvalidExtraProperty) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "access_log" : "path/to/access/log",
    "test" : "a"
  }
  )EOF";

  handleInvalidConfiguration(json_string);
}

TEST_F(MongoFilterConfigTest, EmptyConfig) { handleInvalidConfiguration("{}"); }

TEST_F(MongoFilterConfigTest, InvalidFaultsEmptyConfig) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "fault" : {}
  }
  )EOF";

  handleInvalidConfiguration(json_string);
}

TEST_F(MongoFilterConfigTest, InvalidFaultsMissingPercentage) {
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

TEST_F(MongoFilterConfigTest, InvalidFaultsMissingMs) {
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

TEST_F(MongoFilterConfigTest, InvalidFaultsNegativeMs) {
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

TEST_F(MongoFilterConfigTest, InvalidFaultsDelayPercent) {
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

TEST_F(MongoFilterConfigTest, InvalidFaultsType) {
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

TEST_F(MongoFilterConfigTest, CorrectFaultConfiguration) {
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
  NiceMock<Server::Configuration::MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactory(*json_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

TEST_F(MongoFilterConfigTest, CorrectFaultConfigurationInProto) {
  envoy::config::filter::network::mongo_proxy::v2::MongoProxy config{};
  config.set_stat_prefix("my_stat_prefix");
  config.mutable_delay()->mutable_percentage()->set_numerator(50);
  config.mutable_delay()->mutable_percentage()->set_denominator(
      envoy::type::FractionalPercent::HUNDRED);
  config.mutable_delay()->mutable_fixed_delay()->set_seconds(500);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
