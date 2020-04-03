#include <pthread.h>

#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/network_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

class PostgreSQLIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public BaseIntegrationTest {

  std::string postgres_config() {
    return fmt::format(
        TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
            "test/extensions/filters/network/postgresql_proxy/postgresql_test_config.yaml")),
        Network::Test::getLoopbackAddressString(GetParam()),
        Network::Test::getLoopbackAddressString(GetParam()),
        Network::Test::getAnyAddressString(GetParam()));
  }

public:
  PostgreSQLIntegrationTest() : BaseIntegrationTest(GetParam(), postgres_config()){};

  void SetUp() override { BaseIntegrationTest::initialize(); }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};
INSTANTIATE_TEST_SUITE_P(IpVersions, PostgreSQLIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Test that the filter is properly chained and reacts to successful login
// message.
TEST_P(PostgreSQLIntegrationTest, Login) {
  std::string str;
  std::string recv;

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // send the startup message
  Buffer::OwnedImpl data;
  uint32_t length = htonl(12);
  std::string rcvd;
  char buf[32];

  memset(buf, 0, sizeof(buf));
  data.add(&length, sizeof(length));
  // add 8 bytes of some data
  data.add(buf, 8);
  tcp_client->write(data.toString());
  ASSERT_TRUE(fake_upstream_connection->waitForData(data.toString().length(), &rcvd));
  data.drain(data.length());

  // TCP session is up. Just send the AuthenticationOK downstream
  data.add("R");
  length = htonl(8);
  data.add(&length, sizeof(length));
  uint32_t code = 0;
  data.add(&code, sizeof(code));

  rcvd.clear();
  ASSERT_TRUE(fake_upstream_connection->write(data.toString()));
  rcvd.append(data.toString());
  tcp_client->waitForData(rcvd, true);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  // Make sure that the successful login bumped up the number of sessions.
  test_server_->waitForCounterEq("postgresql.postgresql_stats.sessions", 1);
}

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
