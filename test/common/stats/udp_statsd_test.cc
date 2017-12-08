#include <chrono>

#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/stats/statsd.h"

#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Stats {
namespace Statsd {

class MockWriter : public Writer {
public:
  MOCK_METHOD1(write, void(const std::string& m));
};

class UdpStatsdSinkTest : public testing::TestWithParam<Network::Address::IpVersion> {};
INSTANTIATE_TEST_CASE_P(IpVersions, UdpStatsdSinkTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(UdpStatsdSinkTest, InitWithIpAddress) {
  NiceMock<ThreadLocal::MockInstance> tls_;
  // UDP statsd server address.
  Network::Address::InstanceConstSharedPtr server_address =
      Network::Utility::parseInternetAddressAndPort(
          fmt::format("{}:8125", Network::Test::getLoopbackAddressUrlString(GetParam())));
  UdpStatsdSink sink(tls_, server_address, false);
  int fd = sink.getFdForTests();
  EXPECT_NE(fd, -1);

  // Check that fd has not changed.
  std::vector<Tag> tags;
  NiceMock<MockCounter> counter;
  counter.name_ = "test_counter";
  ON_CALL(counter, tags()).WillByDefault(ReturnRef(tags));
  sink.flushCounter(counter, 1);

  NiceMock<MockGauge> gauge;
  gauge.name_ = "test_gauge";
  ON_CALL(gauge, tags()).WillByDefault(ReturnRef(tags));
  sink.flushGauge(gauge, 1);

  NiceMock<MockHistogram> timer;
  timer.name_ = "test_timer";
  ON_CALL(timer, tags()).WillByDefault(ReturnRef(tags));
  sink.onHistogramComplete(timer, 5);

  EXPECT_EQ(fd, sink.getFdForTests());

  if (GetParam() == Network::Address::IpVersion::v4) {
    EXPECT_EQ("127.0.0.1:8125", Network::Address::peerAddressFromFd(fd)->asString());
  } else {
    EXPECT_EQ("[::1]:8125", Network::Address::peerAddressFromFd(fd)->asString());
  }
  tls_.shutdownThread();
}

class UdpStatsdSinkWithTagsTest : public testing::TestWithParam<Network::Address::IpVersion> {};
INSTANTIATE_TEST_CASE_P(IpVersions, UdpStatsdSinkWithTagsTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(UdpStatsdSinkWithTagsTest, InitWithIpAddress) {
  NiceMock<ThreadLocal::MockInstance> tls_;
  // UDP statsd server address.
  Network::Address::InstanceConstSharedPtr server_address =
      Network::Utility::parseInternetAddressAndPort(
          fmt::format("{}:8125", Network::Test::getLoopbackAddressUrlString(GetParam())));
  UdpStatsdSink sink(tls_, server_address, true);
  int fd = sink.getFdForTests();
  EXPECT_NE(fd, -1);

  // Check that fd has not changed.
  std::vector<Tag> tags = {Tag{"node", "test"}};
  std::string name = "test_counter";
  NiceMock<MockCounter> counter;
  counter.name_ = name;
  ON_CALL(counter, tagExtractedName()).WillByDefault(ReturnRef(name));
  ON_CALL(counter, tags()).WillByDefault(ReturnRef(tags));

  sink.flushCounter(counter, 1);

  name = "test_gauge";
  NiceMock<MockGauge> gauge;
  gauge.name_ = name;
  ON_CALL(gauge, tagExtractedName()).WillByDefault(ReturnRef(name));
  ON_CALL(gauge, tags()).WillByDefault(ReturnRef(tags));
  sink.flushGauge(gauge, 1);

  name = "test_timer";
  NiceMock<MockHistogram> timer;
  timer.name_ = name;
  ON_CALL(timer, tagExtractedName()).WillByDefault(ReturnRef(name));
  ON_CALL(timer, tags()).WillByDefault(ReturnRef(tags));
  sink.onHistogramComplete(timer, 5);

  EXPECT_EQ(fd, sink.getFdForTests());

  if (GetParam() == Network::Address::IpVersion::v4) {
    EXPECT_EQ("127.0.0.1:8125", Network::Address::peerAddressFromFd(fd)->asString());
  } else {
    EXPECT_EQ("[::1]:8125", Network::Address::peerAddressFromFd(fd)->asString());
  }
  tls_.shutdownThread();
}

TEST(UdpStatsdSinkTest, CheckActualStats) {
  auto writer_ptr = std::make_shared<NiceMock<MockWriter>>();
  NiceMock<ThreadLocal::MockInstance> tls_;
  UdpStatsdSink sink(tls_, writer_ptr, false);

  std::vector<Tag> tags;
  NiceMock<MockCounter> counter;
  counter.name_ = "test_counter";
  ON_CALL(counter, tags()).WillByDefault(ReturnRef(tags));
  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.test_counter:1|c"));
  ;
  sink.flushCounter(counter, 1);

  NiceMock<MockGauge> gauge;
  gauge.name_ = "test_gauge";
  ON_CALL(gauge, tags()).WillByDefault(ReturnRef(tags));
  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.test_gauge:1|g"));
  ;
  sink.flushGauge(gauge, 1);

  NiceMock<MockHistogram> timer;
  timer.name_ = "test_timer";
  ON_CALL(timer, tags()).WillByDefault(ReturnRef(tags));
  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.test_timer:5|ms"));
  ;
  sink.onHistogramComplete(timer, 5);

  tls_.shutdownThread();
}

TEST(UdpStatsdSinkWithTagsTest, CheckActualStats) {
  auto writer_ptr = std::make_shared<NiceMock<MockWriter>>();
  NiceMock<ThreadLocal::MockInstance> tls_;
  UdpStatsdSink sink(tls_, writer_ptr, true);

  std::vector<Tag> tags = {Tag{"key1", "value1"}, Tag{"key2", "value2"}};
  std::string name = "test_counter";
  NiceMock<MockCounter> counter;
  counter.name_ = name;
  ON_CALL(counter, tagExtractedName()).WillByDefault(ReturnRef(name));
  ON_CALL(counter, tags()).WillByDefault(ReturnRef(tags));
  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.test_counter:1|c|#key1:value1,key2:value2"));
  ;
  sink.flushCounter(counter, 1);

  NiceMock<MockGauge> gauge;
  name = "test_gauge";
  gauge.name_ = name;
  ON_CALL(gauge, tagExtractedName()).WillByDefault(ReturnRef(name));
  ON_CALL(gauge, tags()).WillByDefault(ReturnRef(tags));
  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.test_gauge:1|g|#key1:value1,key2:value2"));
  ;
  sink.flushGauge(gauge, 1);

  NiceMock<MockHistogram> timer;
  name = "test_timer";
  timer.name_ = name;
  ON_CALL(timer, tagExtractedName()).WillByDefault(ReturnRef(name));
  ON_CALL(timer, tags()).WillByDefault(ReturnRef(tags));
  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.test_timer:5|ms|#key1:value1,key2:value2"));
  ;
  sink.onHistogramComplete(timer, 5);

  tls_.shutdownThread();
}

} // namespace Statsd
} // namespace Stats
} // namespace Envoy
