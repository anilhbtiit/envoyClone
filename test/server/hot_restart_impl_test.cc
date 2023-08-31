#include <memory>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/api/os_sys_calls_impl_hot_restart.h"
#include "source/common/common/hex.h"
#include "source/common/network/utility.h"
#include "source/server/hot_restart_impl.h"

#include "test/mocks/api/hot_restart.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/hot_restart.h"
#include "test/test_common/logging.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::WithArg;

namespace Envoy {
namespace Server {

struct TestAddresses {
  Network::Address::InstanceConstSharedPtr ipv4_test_addr_ =
      Network::Utility::parseInternetAddressAndPort("127.0.0.5:12345");
  Network::Address::InstanceConstSharedPtr ipv4_test_addr_different_ip_ =
      Network::Utility::parseInternetAddressAndPort("127.0.0.6:12345");
  Network::Address::InstanceConstSharedPtr ipv4_test_addr_different_port_ =
      Network::Utility::parseInternetAddressAndPort("127.0.0.5:12346");
  Network::Address::InstanceConstSharedPtr ipv4_default_ =
      Network::Utility::parseInternetAddressAndPort("0.0.0.0:12345");
  Network::Address::InstanceConstSharedPtr ipv6_test_addr_ =
      Network::Utility::parseInternetAddressAndPort("[::1]:12345");
  Network::Address::InstanceConstSharedPtr ipv6_test_addr_different_ip_ =
      Network::Utility::parseInternetAddressAndPort("[::2]:12345");
  Network::Address::InstanceConstSharedPtr ipv6_test_addr_different_port_ =
      Network::Utility::parseInternetAddressAndPort("[::1]:12346");
  Network::Address::InstanceConstSharedPtr ipv6_default_ =
      Network::Utility::parseInternetAddressAndPort("[::]:12345");
};

class HotRestartImplTest : public testing::Test {
public:
  void setup() {
    EXPECT_CALL(hot_restart_os_sys_calls_, shmUnlink(_)).Times(AnyNumber());
    EXPECT_CALL(hot_restart_os_sys_calls_, shmOpen(_, _, _));
    EXPECT_CALL(os_sys_calls_, ftruncate(_, _)).WillOnce(WithArg<1>(Invoke([this](off_t size) {
      buffer_.resize(size);
      return Api::SysCallIntResult{0, 0};
    })));
    EXPECT_CALL(os_sys_calls_, mmap(_, _, _, _, _, _)).WillOnce(InvokeWithoutArgs([this]() {
      return Api::SysCallPtrResult{buffer_.data(), 0};
    }));
    // We bind two sockets: one to talk to parent, one to talk to our (hypothetical eventual) child
    EXPECT_CALL(os_sys_calls_, bind(_, _, _)).Times(2);

    // Test we match the correct stat with empty-slots before, after, or both.
    hot_restart_ = std::make_unique<HotRestartImpl>(0, 0, "@envoy_domain_socket", 0);
    hot_restart_->drainParentListeners();

    // We close both sockets.
    EXPECT_CALL(os_sys_calls_, close(_)).Times(2);
  }

  // test_addresses_ must be initialized before os_sys_calls_ sets us mocking, as
  // parseInternetAddress uses several os system calls.
  TestAddresses test_addresses_;
  Api::MockOsSysCalls os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls_};
  Api::MockHotRestartOsSysCalls hot_restart_os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::HotRestartOsSysCallsImpl> hot_restart_os_calls{
      &hot_restart_os_sys_calls_};
  std::vector<uint8_t> buffer_;
  std::unique_ptr<HotRestartImpl> hot_restart_;
};

TEST_F(HotRestartImplTest, VersionString) {
  // Tests that the version-string will be consistent and HOT_RESTART_VERSION,
  // between multiple instantiations.
  std::string version;

  // The mocking infrastructure requires a test setup and teardown every time we
  // want to re-instantiate HotRestartImpl.
  {
    setup();
    version = hot_restart_->version();
    EXPECT_TRUE(absl::StartsWith(version, fmt::format("{}.", HOT_RESTART_VERSION))) << version;
    TearDown();
  }

  {
    setup();
    EXPECT_EQ(version, hot_restart_->version()) << "Version string deterministic from options";
    TearDown();
  }
}

// Test that HotRestartDomainSocketInUseException is thrown when the domain socket is already
// in use,
TEST_F(HotRestartImplTest, DomainSocketAlreadyInUse) {
  EXPECT_CALL(os_sys_calls_, bind(_, _, _))
      .WillOnce(Return(Api::SysCallIntResult{-1, SOCKET_ERROR_ADDR_IN_USE}));
  EXPECT_CALL(os_sys_calls_, close(_));

  EXPECT_THROW(std::make_unique<HotRestartImpl>(0, 0, "@envoy_domain_socket", 0),
               Server::HotRestartDomainSocketInUseException);
}

// Test that EnvoyException is thrown when the domain socket bind fails for reasons other than
// being in use.
TEST_F(HotRestartImplTest, DomainSocketError) {
  EXPECT_CALL(os_sys_calls_, bind(_, _, _))
      .WillOnce(Return(Api::SysCallIntResult{-1, SOCKET_ERROR_ACCESS}));
  EXPECT_CALL(os_sys_calls_, close(_));

  EXPECT_THROW(std::make_unique<HotRestartImpl>(0, 0, "@envoy_domain_socket", 0), EnvoyException);
}

class HotRestartUdpForwardingContextTest : public HotRestartImplTest {
public:
  void SetUp() override { setup(); }

  void childRegisterUdpForwardingListener(const Network::Address::Instance& address,
                                          Network::UdpListenerConfig& listener_config) {
    hot_restart_->as_child_.registerUdpForwardingListener(address, listener_config);
  }
  absl::optional<HotRestartingChild::UdpForwardingContext::ForwardEntry>
  childGetListenerForDestination(const Network::Address::Instance& address) {
    return hot_restart_->as_child_.udp_forwarding_context_.getListenerForDestination(address);
  }
};

// Test that registering a forwarding listener results in a UdpForwardingContext which
// returns the correct listener, for IPv4 addresses.
TEST_F(HotRestartUdpForwardingContextTest, RegisterUdpForwardingListenerFindsIpv4Address) {
  Network::MockUdpListenerConfig config_1;
  Network::MockUdpListenerConfig config_any;
  childRegisterUdpForwardingListener(*test_addresses_.ipv4_test_addr_, config_1);
  childRegisterUdpForwardingListener(*test_addresses_.ipv4_default_, config_any);
  // Try a request to the specified address and port.
  auto result = childGetListenerForDestination(*test_addresses_.ipv4_test_addr_);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first->asStringView(), test_addresses_.ipv4_test_addr_->asStringView());
  EXPECT_EQ(result->second, &config_1);
  // Try with mismatched port, should be no result.
  result = childGetListenerForDestination(*test_addresses_.ipv4_test_addr_different_port_);
  EXPECT_FALSE(result.has_value());
  // Try with mismatched address, should be default route.
  result = childGetListenerForDestination(*test_addresses_.ipv4_test_addr_different_ip_);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first->asStringView(), test_addresses_.ipv4_default_->asStringView());
  EXPECT_EQ(result->second, &config_any);
  // If there's an IPv6 request and only an IPv4 default route, use that route.
  result = childGetListenerForDestination(*test_addresses_.ipv6_test_addr_);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first->asStringView(), test_addresses_.ipv4_default_->asStringView());
  EXPECT_EQ(result->second, &config_any);
}

// Test that registering a forwarding listener results in a UdpForwardingContext which
// returns the correct listener, for IPv6 addresses.
TEST_F(HotRestartUdpForwardingContextTest, RegisterUdpForwardingListenerFindsIpv6Address) {
  Network::MockUdpListenerConfig config_1;
  Network::MockUdpListenerConfig config_any;
  childRegisterUdpForwardingListener(*test_addresses_.ipv6_test_addr_, config_1);
  childRegisterUdpForwardingListener(*test_addresses_.ipv6_default_, config_any);
  // Try a request to the specified address and port.
  auto result = childGetListenerForDestination(*test_addresses_.ipv6_test_addr_);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first->asStringView(), test_addresses_.ipv6_test_addr_->asStringView());
  EXPECT_EQ(result->second, &config_1);
  // Try with mismatched port, should be no result.
  result = childGetListenerForDestination(*test_addresses_.ipv6_test_addr_different_port_);
  EXPECT_FALSE(result.has_value());
  // Try with mismatched address, should be default route.
  result = childGetListenerForDestination(*test_addresses_.ipv6_test_addr_different_ip_);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first->asStringView(), test_addresses_.ipv6_default_->asStringView());
  EXPECT_EQ(result->second, &config_any);
  // If there's an IPv6 request and only an IPv4 default route, use that route.
  result = childGetListenerForDestination(*test_addresses_.ipv4_test_addr_);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first->asStringView(), test_addresses_.ipv6_default_->asStringView());
  EXPECT_EQ(result->second, &config_any);
}

// Test that registering a udp forwarding listener default route for IPv6 and
// IPv6 separately prefers the one that matches the type of the request.
TEST_F(HotRestartUdpForwardingContextTest,
       RegisterUdpForwardingListenerPrefersSameTypeDefaultRoute) {
  Network::MockUdpListenerConfig config_ip4;
  Network::MockUdpListenerConfig config_ip6;
  childRegisterUdpForwardingListener(*test_addresses_.ipv4_default_, config_ip4);
  childRegisterUdpForwardingListener(*test_addresses_.ipv6_default_, config_ip6);
  // Request to an IPv6 address should use the ip6 config.
  auto result = childGetListenerForDestination(*test_addresses_.ipv6_test_addr_);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first->asStringView(), test_addresses_.ipv6_default_->asStringView());
  EXPECT_EQ(result->second, &config_ip6);
  // Request to an IPv4 address should use the ip4 config.
  result = childGetListenerForDestination(*test_addresses_.ipv4_test_addr_);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first->asStringView(), test_addresses_.ipv4_default_->asStringView());
  EXPECT_EQ(result->second, &config_ip4);
  // Request to a different port should be not matched.
  result = childGetListenerForDestination(*test_addresses_.ipv4_test_addr_different_port_);
  EXPECT_FALSE(result.has_value());
  result = childGetListenerForDestination(*test_addresses_.ipv6_test_addr_different_port_);
  EXPECT_FALSE(result.has_value());
}

} // namespace Server
} // namespace Envoy
