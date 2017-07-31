#include <cstdint>
#include <list>
#include <string>

#include "envoy/common/exception.h"

#include "common/common/thread.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {

TEST(NetworkUtility, Url) {
  EXPECT_EQ("foo", Utility::hostFromTcpUrl("tcp://foo:1234"));
  EXPECT_EQ(1234U, Utility::portFromTcpUrl("tcp://foo:1234"));
  EXPECT_THROW(Utility::hostFromTcpUrl("bogus://foo:1234"), EnvoyException);
  EXPECT_THROW(Utility::portFromTcpUrl("bogus://foo:1234"), EnvoyException);
  EXPECT_THROW(Utility::hostFromTcpUrl("abc://foo"), EnvoyException);
  EXPECT_THROW(Utility::portFromTcpUrl("abc://foo"), EnvoyException);
  EXPECT_THROW(Utility::hostFromTcpUrl("tcp://foo"), EnvoyException);
  EXPECT_THROW(Utility::portFromTcpUrl("tcp://foo"), EnvoyException);
  EXPECT_THROW(Utility::portFromTcpUrl("tcp://foo:bar"), EnvoyException);
  EXPECT_THROW(Utility::hostFromTcpUrl(""), EnvoyException);
}

TEST(NetworkUtility, resolveUrl) {
  EXPECT_THROW(Utility::resolveUrl("foo"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("abc://foo"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://1.2.3.4:1234/"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://127.0.0.1:8001/"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://127.0.0.1:0/foo"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://127.0.0.1:"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://192.168.3.3"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://192.168.3.3.3:0"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://192.168.3:0"), EnvoyException);

  EXPECT_THROW(Utility::resolveUrl("tcp://[::1]"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://[:::1]:1"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://foo:0"), EnvoyException);

  EXPECT_EQ("", Utility::resolveUrl("unix://")->asString());
  EXPECT_EQ("foo", Utility::resolveUrl("unix://foo")->asString());
  EXPECT_EQ("tmp", Utility::resolveUrl("unix://tmp")->asString());
  EXPECT_EQ("tmp/server", Utility::resolveUrl("unix://tmp/server")->asString());

  EXPECT_EQ("1.2.3.4:1234", Utility::resolveUrl("tcp://1.2.3.4:1234")->asString());
  EXPECT_EQ("0.0.0.0:0", Utility::resolveUrl("tcp://0.0.0.0:0")->asString());
  EXPECT_EQ("127.0.0.1:0", Utility::resolveUrl("tcp://127.0.0.1:0")->asString());

  EXPECT_EQ("[::1]:1", Utility::resolveUrl("tcp://[::1]:1")->asString());
  EXPECT_EQ("[::]:0", Utility::resolveUrl("tcp://[::]:0")->asString());
  EXPECT_EQ("[1::2:3]:4", Utility::resolveUrl("tcp://[1::2:3]:4")->asString());
  EXPECT_EQ("[a::1]:0", Utility::resolveUrl("tcp://[a::1]:0")->asString());
  EXPECT_EQ("[a:b:c:d::]:0", Utility::resolveUrl("tcp://[a:b:c:d::]:0")->asString());
}

TEST(NetworkUtility, ParseInternetAddress) {
  EXPECT_THROW(Utility::parseInternetAddress(""), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("1.2.3"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("1.2.3.4.5"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("1.2.3.256"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("foo"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("0:0:0:0"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("fffff::"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("/foo"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("[::]"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("[::1]:1"), EnvoyException);

  EXPECT_EQ("1.2.3.4:0", Utility::parseInternetAddress("1.2.3.4")->asString());
  EXPECT_EQ("0.0.0.0:0", Utility::parseInternetAddress("0.0.0.0")->asString());
  EXPECT_EQ("127.0.0.1:0", Utility::parseInternetAddress("127.0.0.1")->asString());

  EXPECT_EQ("[::1]:0", Utility::parseInternetAddress("::1")->asString());
  EXPECT_EQ("[::]:0", Utility::parseInternetAddress("::")->asString());
  EXPECT_EQ("[1::2:3]:0", Utility::parseInternetAddress("1::2:3")->asString());
  EXPECT_EQ("[a::1]:0", Utility::parseInternetAddress("a::1")->asString());
  EXPECT_EQ("[a:b:c:d::]:0", Utility::parseInternetAddress("a:b:c:d::")->asString());
}

TEST(NetworkUtility, ParseInternetAddressAndPort) {
  EXPECT_THROW(Utility::parseInternetAddressAndPort("1.2.3.4"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("1.2.3.4:"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("1.2.3.4::1"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("1.2.3.4:-1"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort(":1"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort(" :1"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("1.2.3:1"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("1.2.3.4]:2"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("1.2.3.4:65536"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("1.2.3.4:8008/"), EnvoyException);

  EXPECT_EQ("0.0.0.0:0", Utility::parseInternetAddressAndPort("0.0.0.0:0")->asString());
  EXPECT_EQ("255.255.255.255:65535",
            Utility::parseInternetAddressAndPort("255.255.255.255:65535")->asString());
  EXPECT_EQ("127.0.0.1:0", Utility::parseInternetAddressAndPort("127.0.0.1:0")->asString());

  EXPECT_THROW(Utility::parseInternetAddressAndPort(""), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("::1"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("::"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("[[::]]:1"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("[::]:1]:2"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("]:[::1]:2"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("[1.2.3.4:0"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("[1.2.3.4]:0"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("[::]:"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("[::]:-1"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("[::]:bogus"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("[1::1]:65536"), EnvoyException);

  EXPECT_EQ("[::]:0", Utility::parseInternetAddressAndPort("[::]:0")->asString());
  EXPECT_EQ("[1::1]:65535", Utility::parseInternetAddressAndPort("[1::1]:65535")->asString());
  EXPECT_EQ("[::1]:0", Utility::parseInternetAddressAndPort("[::1]:0")->asString());
}

class NetworkUtilityGetLocalAddress : public testing::TestWithParam<Address::IpVersion> {};

INSTANTIATE_TEST_CASE_P(IpVersions, NetworkUtilityGetLocalAddress,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(NetworkUtilityGetLocalAddress, GetLocalAddress) {
  EXPECT_NE(nullptr, Utility::getLocalAddress(GetParam()));
}

TEST(NetworkUtility, GetOriginalDstBadFd) { EXPECT_EQ(nullptr, Utility::getOriginalDst(-1)); }

class NetworkUtilityGetOriginalDst : public testing::TestWithParam<Address::IpVersion> {
public:
  NetworkUtilityGetOriginalDst() {
    EXPECT_EQ(pthread_mutex_init(&mutex_, nullptr), 0);
    EXPECT_EQ(pthread_cond_init(&cond_, nullptr), 0);
  }

  ~NetworkUtilityGetOriginalDst() {
    EXPECT_EQ(pthread_cond_destroy(&cond_), 0);
    EXPECT_EQ(pthread_mutex_destroy(&mutex_), 0);
  }

  void makeFdBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ASSERT_GE(flags, 0);
    ASSERT_EQ(::fcntl(fd, F_SETFL, flags & (~O_NONBLOCK)), 0);
  }

  void wait() {
    EXPECT_EQ(pthread_mutex_lock(&mutex_), 0);

    int rc = 0;
    while(rc == 0 && !signaled_) {
      rc = pthread_cond_wait(&cond_, &mutex_);
    }

    EXPECT_EQ(pthread_mutex_unlock(&mutex_), 0);
    EXPECT_EQ(rc, 0);
  }

  void signal() {
    EXPECT_EQ(pthread_mutex_lock(&mutex_), 0);
    signaled_ = 1;
    EXPECT_EQ(pthread_cond_signal(&cond_), 0);
    EXPECT_EQ(pthread_mutex_unlock(&mutex_), 0);
  }

protected:
  pthread_mutex_t mutex_;
  pthread_cond_t cond_;
  bool signaled_ = 0;
};

INSTANTIATE_TEST_CASE_P(IpVersions, NetworkUtilityGetOriginalDst,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(NetworkUtilityGetOriginalDst, GetOriginalDst) {
  const std::string& addr_port_str =
      fmt::format("{}:0", Network::Test::getLoopbackAddressUrlString(GetParam()));

  auto addr_port = Network::Utility::parseInternetAddressAndPort(addr_port_str);
  ASSERT_NE(addr_port, nullptr);
  if (addr_port->ip()->port() == 0) {
    addr_port = Network::Test::findOrCheckFreePort(addr_port, Address::SocketType::Stream);
  }
  ASSERT_NE(addr_port, nullptr);
  ASSERT_NE(addr_port->ip(), nullptr);

  // Create a socket on which we'll listen for connections from clients.
  const int listen_fd = addr_port->socket(Address::SocketType::Stream);
  ASSERT_GE(listen_fd, 0) << addr_port->asString();
  ScopedFdCloser closer1(listen_fd);

  makeFdBlocking(listen_fd);

  // Check that IPv6 sockets accept IPv6 connections only.
  if (addr_port->ip()->version() == Address::IpVersion::v6) {
    int v6only = 0;
    socklen_t size_int = sizeof(v6only);
    ASSERT_GE(::getsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, &size_int), 0);
    EXPECT_EQ(v6only, 1);
  }

  // Bind the socket to the desired address and port.
  int rc = addr_port->bind(listen_fd);
  int err = errno;
  ASSERT_EQ(rc, 0) << addr_port->asString() << "\nerror: " << strerror(err) << "\nerrno: " << err;

  ASSERT_EQ(::listen(listen_fd, 1), 0);

  auto thread = std::unique_ptr<Thread::Thread>();
  thread.reset(new Thread::Thread([&]() -> void {
    int client_fd = addr_port->socket(Address::SocketType::Stream);
    EXPECT_GE(client_fd, 0);
    ScopedFdCloser closer2(client_fd);

    makeFdBlocking(client_fd);

    int rc = addr_port->connect(client_fd);
    EXPECT_EQ(rc, 0);

    wait();
  }));

  int fd = ::accept(listen_fd, nullptr, nullptr);
  err = errno;
  ASSERT_GE(fd, 0) << "\naccept error: " << strerror(err) << "\nerrno: " << err;
  ScopedFdCloser closer3(fd);

  auto orig_dst = Utility::getOriginalDst(fd);

  signal();
  thread->join();

#if !defined(__APPLE__)
  // TODO(zuercher): Remove this case when non-OS X implementations of getOriginalDst are fixed
  if (GetParam() == Address::IpVersion::v6) {
    EXPECT_EQ(nullptr, orig_dst);
    return;
  }
#endif

  EXPECT_EQ(addr_port->asString(), orig_dst->asString());
}

TEST(NetworkUtility, InternalAddress) {
  EXPECT_TRUE(Utility::isInternalAddress("127.0.0.1"));
  EXPECT_TRUE(Utility::isInternalAddress("10.0.0.1"));
  EXPECT_TRUE(Utility::isInternalAddress("192.168.0.0"));
  EXPECT_TRUE(Utility::isInternalAddress("172.16.0.0"));
  EXPECT_TRUE(Utility::isInternalAddress("172.30.2.1"));
  EXPECT_FALSE(Utility::isInternalAddress(""));
  EXPECT_FALSE(Utility::isInternalAddress("127"));
  EXPECT_FALSE(Utility::isInternalAddress("192.167.0.0"));
  EXPECT_FALSE(Utility::isInternalAddress("172.32.0.0"));
  EXPECT_FALSE(Utility::isInternalAddress("11.0.0.1"));

  EXPECT_TRUE(Utility::isInternalAddress("fd00::"));
  EXPECT_TRUE(Utility::isInternalAddress("::1"));
  EXPECT_TRUE(Utility::isInternalAddress("fdff::"));
  EXPECT_TRUE(Utility::isInternalAddress("fd01::"));
  EXPECT_TRUE(Utility::isInternalAddress("fd12:3456:7890:1234:5678:9012:3456:7890"));
  EXPECT_FALSE(Utility::isInternalAddress("fd::"));
  EXPECT_FALSE(Utility::isInternalAddress("::"));
  EXPECT_FALSE(Utility::isInternalAddress("fc00::"));
  EXPECT_FALSE(Utility::isInternalAddress("fe00::"));
  EXPECT_FALSE(Utility::isInternalAddress("fd00:::"));
}

TEST(NetworkUtility, LoopbackAddress) {
  {
    Address::Ipv4Instance address("127.0.0.1");
    EXPECT_TRUE(Utility::isLoopbackAddress(address));
  }
  {
    Address::Ipv4Instance address("10.0.0.1");
    EXPECT_FALSE(Utility::isLoopbackAddress(address));
  }
  {
    Address::PipeInstance address("/foo");
    EXPECT_FALSE(Utility::isLoopbackAddress(address));
  }
  {
    Address::Ipv6Instance address("::1");
    EXPECT_TRUE(Utility::isLoopbackAddress(address));
  }
  {
    Address::Ipv6Instance address("::");
    EXPECT_FALSE(Utility::isLoopbackAddress(address));
  }
  EXPECT_EQ("127.0.0.1:0", Utility::getCanonicalIpv4LoopbackAddress()->asString());
  EXPECT_EQ("[::1]:0", Utility::getIpv6LoopbackAddress()->asString());
}

TEST(NetworkUtility, AnyAddress) {
  {
    Address::InstanceConstSharedPtr any = Utility::getIpv4AnyAddress();
    ASSERT_NE(any, nullptr);
    EXPECT_EQ(any->type(), Address::Type::Ip);
    EXPECT_EQ(any->ip()->version(), Address::IpVersion::v4);
    EXPECT_EQ(any->asString(), "0.0.0.0:0");
    EXPECT_EQ(any, Utility::getIpv4AnyAddress());
  }
  {
    Address::InstanceConstSharedPtr any = Utility::getIpv6AnyAddress();
    ASSERT_NE(any, nullptr);
    EXPECT_EQ(any->type(), Address::Type::Ip);
    EXPECT_EQ(any->ip()->version(), Address::IpVersion::v6);
    EXPECT_EQ(any->asString(), "[::]:0");
    EXPECT_EQ(any, Utility::getIpv6AnyAddress());
  }
}

TEST(PortRangeListTest, Errors) {
  {
    std::string port_range_str = "a1";
    std::list<PortRange> port_range_list;
    EXPECT_THROW(Utility::parsePortRangeList(port_range_str, port_range_list), EnvoyException);
  }

  {
    std::string port_range_str = "1A";
    std::list<PortRange> port_range_list;
    EXPECT_THROW(Utility::parsePortRangeList(port_range_str, port_range_list), EnvoyException);
  }

  {
    std::string port_range_str = "1_1";
    std::list<PortRange> port_range_list;
    EXPECT_THROW(Utility::parsePortRangeList(port_range_str, port_range_list), EnvoyException);
  }

  {
    std::string port_range_str = "1,1X1";
    std::list<PortRange> port_range_list;
    EXPECT_THROW(Utility::parsePortRangeList(port_range_str, port_range_list), EnvoyException);
  }

  {
    std::string port_range_str = "1,1*1";
    std::list<PortRange> port_range_list;
    EXPECT_THROW(Utility::parsePortRangeList(port_range_str, port_range_list), EnvoyException);
  }
}

static Address::Ipv4Instance makeFromPort(uint32_t port) {
  return Address::Ipv4Instance("0.0.0.0", port);
}

TEST(PortRangeListTest, Normal) {
  {
    std::string port_range_str = "1";
    std::list<PortRange> port_range_list;

    Utility::parsePortRangeList(port_range_str, port_range_list);
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(1), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(2), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(Address::PipeInstance("/foo"), port_range_list));
  }

  {
    std::string port_range_str = "1024-2048";
    std::list<PortRange> port_range_list;

    Utility::parsePortRangeList(port_range_str, port_range_list);
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(1024), port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(2048), port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(1536), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(1023), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(2049), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(0), port_range_list));
  }

  {
    std::string port_range_str = "1,10-100,1000-10000,65535";
    std::list<PortRange> port_range_list;

    Utility::parsePortRangeList(port_range_str, port_range_list);
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(1), port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(50), port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(5000), port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(65535), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(2), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(200), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(20000), port_range_list));
  }
}

} // namespace Network
} // namespace Envoy
