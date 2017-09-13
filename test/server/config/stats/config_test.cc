#include <string>

#include "envoy/registry/registry.h"

#include "common/config/well_known_names.h"
#include "common/protobuf/utility.h"
#include "common/stats/statsd.h"

#include "server/config/stats/statsd.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "api/bootstrap.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Server {
namespace Configuration {

TEST(StatsConfigTest, ValidTcpStatsd) {
  const std::string name = Config::StatsSinkNames::get().STATSD;

  envoy::api::v2::StatsdSink sink_config;
  sink_config.set_tcp_cluster_name("fake_cluster");

  StatsSinkFactory* factory = Registry::FactoryRegistry<StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  MessageUtil::jsonConvert(sink_config, *message);

  NiceMock<MockInstance> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  EXPECT_NE(sink, nullptr);
  EXPECT_NE(dynamic_cast<Stats::Statsd::TcpStatsdSink*>(sink.get()), nullptr);
}

class StatsConfigLoopbackTest : public testing::TestWithParam<Network::Address::IpVersion> {};
INSTANTIATE_TEST_CASE_P(IpVersions, StatsConfigLoopbackTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(StatsConfigLoopbackTest, ValidUdpIpStatsd) {
  const std::string name = Config::StatsSinkNames::get().STATSD;

  envoy::api::v2::StatsdSink sink_config;
  envoy::api::v2::Address& address = *sink_config.mutable_address();
  envoy::api::v2::SocketAddress& socket_address = *address.mutable_socket_address();
  socket_address.set_protocol(envoy::api::v2::SocketAddress::UDP);
  auto loopback_flavor = Network::Test::getCanonicalLoopbackAddress(GetParam());
  socket_address.set_address(loopback_flavor->ip()->addressAsString());
  socket_address.set_port_value(8125);

  StatsSinkFactory* factory = Registry::FactoryRegistry<StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  MessageUtil::jsonConvert(sink_config, *message);

  NiceMock<MockInstance> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  EXPECT_NE(sink, nullptr);
  EXPECT_NE(dynamic_cast<Stats::Statsd::UdpStatsdSink*>(sink.get()), nullptr);
}

TEST(StatsConfigTest, EmptyConfig) {
  const std::string name = Config::StatsSinkNames::get().STATSD;
  envoy::api::v2::StatsdSink sink_config;

  StatsSinkFactory* factory = Registry::FactoryRegistry<StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  MessageUtil::jsonConvert(sink_config, *message);
  NiceMock<MockInstance> server;
  EXPECT_THROW_WITH_MESSAGE(
      factory->createStatsSink(*message, server), EnvoyException,
      "No tcp_cluster_name or address provided for envoy.statsd Stats::Sink config");
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
