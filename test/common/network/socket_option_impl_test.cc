#include "common/network/address_impl.h"
#include "common/network/socket_option_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Network {
namespace {

class SocketOptionImplTest : public testing::Test {
public:
  SocketOptionImplTest() { socket_.local_address_.reset(); }

  NiceMock<MockListenSocket> socket_;
  Api::MockOsSysCalls os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls_};

  void testSetSocketOptionSuccess(SocketOptionImpl& socket_option, int socket_level,
                                  Network::SocketOptionName option_name, int option_val,
                                  const std::set<Socket::SocketState>& when) {
    Address::Ipv4Instance address("1.2.3.4", 5678);
    const int fd = address.socket(Address::SocketType::Stream);
    EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));

    for (Socket::SocketState state : when) {
      if (option_name.has_value()) {
        EXPECT_CALL(os_sys_calls_,
                    setsockopt_(_, socket_level, option_name.value(), _, sizeof(int)))
            .WillOnce(Invoke([option_val](int, int, int, const void* optval, socklen_t) -> int {
              EXPECT_EQ(option_val, *static_cast<const int*>(optval));
              return 0;
            }));
        EXPECT_TRUE(socket_option.setOption(socket_, state));
      } else {
        EXPECT_FALSE(socket_option.setOption(socket_, state));
      }
    }

    // The set of SocketState for which this option should not be set. Initialize to all
    // the states, and remove states that are passed in.
    std::list<Socket::SocketState> unset_socketstates{
        Socket::SocketState::PreBind,
        Socket::SocketState::PostBind,
        Socket::SocketState::Listening,
    };
    unset_socketstates.remove_if(
        [&](Socket::SocketState state) -> bool { return when.find(state) != when.end(); });
    for (Socket::SocketState state : unset_socketstates) {
      EXPECT_CALL(os_sys_calls_, setsockopt_(_, _, _, _, _)).Times(0);
      EXPECT_TRUE(socket_option.setOption(socket_, state));
    }
  }
};

// We fail to set the option if the socket FD is bad.
TEST_F(SocketOptionImplTest, BadFd) {
  EXPECT_CALL(socket_, fd()).WillOnce(Return(-1));
  EXPECT_EQ(ENOTSUP, SocketOptionImpl::setIpSocketOption(socket_, {}, {}, nullptr, 0));
}

// Nop when there are no socket options set.
TEST_F(SocketOptionImplTest, SetOptionEmptyNop) {
  SocketOptionImpl socket_option{{}, {}, {}};
  EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
  EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PostBind));
  EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::Listening));
}

// We fail to set the option when the underlying setsockopt syscall fails.
TEST_F(SocketOptionImplTest, SetOptionTransparentFailure) {
  SocketOptionImpl socket_option{true, {}, {}};
  EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
}

// We fail to set the option when the underlying setsockopt syscall fails.
TEST_F(SocketOptionImplTest, SetOptionFreebindFailure) {
  SocketOptionImpl socket_option{{}, true, {}};
  EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
}

// We fail to set the tcp-fastopen option when the underlying setsockopt syscall fails.
TEST_F(SocketOptionImplTest, SetOptionTcpFastopenFailure) {
  if (ENVOY_SOCKET_TCP_FASTOPEN.has_value()) {
    SocketOptionImpl socket_option{{}, {}, 1};
    EXPECT_CALL(os_sys_calls_, setsockopt_(_, IPPROTO_TCP, ENVOY_SOCKET_TCP_FASTOPEN.value(), _, _))
        .WillOnce(Return(-1));
    EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::Listening));
  }
}

// The happy path for setOption(); IP_TRANSPARENT is set to true.
TEST_F(SocketOptionImplTest, SetOptionTransparentSuccessTrue) {
  SocketOptionImpl socket_option{true, {}, {}};
  testSetSocketOptionSuccess(socket_option, IPPROTO_IP, ENVOY_SOCKET_IP_TRANSPARENT, 1,
                             {Socket::SocketState::PreBind, Socket::SocketState::PostBind});
}

// The happy path for setOption(); IP_FREEBIND is set to true.
TEST_F(SocketOptionImplTest, SetOptionFreebindSuccessTrue) {
  SocketOptionImpl socket_option{{}, true, {}};
  testSetSocketOptionSuccess(socket_option, IPPROTO_IP, ENVOY_SOCKET_IP_FREEBIND, 1,
                             {Socket::SocketState::PreBind});
}

// The happy path for setOption(); TCP_FASTOPEN is set to true.
TEST_F(SocketOptionImplTest, SetOptionTcpFastopenSuccessTrue) {
  SocketOptionImpl socket_option{{}, {}, 42};
  testSetSocketOptionSuccess(socket_option, IPPROTO_TCP, ENVOY_SOCKET_TCP_FASTOPEN, 42,
                             {Socket::SocketState::Listening});
}

// The happy path for setOpion(); IP_TRANSPARENT is set to false.
TEST_F(SocketOptionImplTest, SetOptionTransparentSuccessFalse) {
  SocketOptionImpl socket_option{false, {}, {}};
  testSetSocketOptionSuccess(socket_option, IPPROTO_IP, ENVOY_SOCKET_IP_TRANSPARENT, 0,
                             {Socket::SocketState::PreBind, Socket::SocketState::PostBind});
}

// The happy path for setOpion(); IP_FREEBIND is set to false.
TEST_F(SocketOptionImplTest, SetOptionFreebindSuccessFalse) {
  SocketOptionImpl socket_option{{}, false, {}};
  testSetSocketOptionSuccess(socket_option, IPPROTO_IP, ENVOY_SOCKET_IP_FREEBIND, 0,
                             {Socket::SocketState::PreBind});
}

// The happy path for setOpion(); TCP_FASTOPEN is set to false.
TEST_F(SocketOptionImplTest, SetOptionTcpFastopenSuccessFalse) {
  SocketOptionImpl socket_option{{}, {}, 0};
  testSetSocketOptionSuccess(socket_option, IPPROTO_IP, ENVOY_SOCKET_TCP_FASTOPEN, 0,
                             {Socket::SocketState::Listening});
}

// If a platform doesn't suppport IPv4 socket option variant for an IPv4 address, we fail
// SocketOptionImpl::setIpSocketOption().
TEST_F(SocketOptionImplTest, V4EmptyOptionNames) {
  Address::Ipv4Instance address("1.2.3.4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  EXPECT_EQ(ENOTSUP, SocketOptionImpl::setIpSocketOption(socket_, {}, {}, nullptr, 0));
}

// If a platform doesn't suppport IPv4 and IPv6 socket option variants for an IPv4 address, we fail
// SocketOptionImpl::setIpSocketOption().
TEST_F(SocketOptionImplTest, V6EmptyOptionNames) {
  Address::Ipv6Instance address("::1:2:3:4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  EXPECT_EQ(ENOTSUP, SocketOptionImpl::setIpSocketOption(socket_, {}, {}, nullptr, 0));
}

// If a platform suppports IPv4 socket option variant for an IPv4 address,
// SocketOptionImpl::setIpSocketOption() works.
TEST_F(SocketOptionImplTest, V4Only) {
  Address::Ipv4Instance address("1.2.3.4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  const int option = 42;
  EXPECT_CALL(os_sys_calls_, setsockopt_(fd, IPPROTO_IP, 123, &option, sizeof(int)));
  EXPECT_EQ(0, SocketOptionImpl::setIpSocketOption(socket_, {123}, {}, &option, sizeof(option)));
}

// If a platform suppports IPv4 and IPv6 socket option variants for an IPv4 address,
// SocketOptionImpl::setIpSocketOption() works with the IPv4 variant.
TEST_F(SocketOptionImplTest, V4IgnoreV6) {
  Address::Ipv4Instance address("1.2.3.4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  const int option = 42;
  EXPECT_CALL(os_sys_calls_, setsockopt_(fd, IPPROTO_IP, 123, &option, sizeof(int)));
  EXPECT_EQ(0, SocketOptionImpl::setIpSocketOption(socket_, {123}, {456}, &option, sizeof(option)));
}

// If a platform suppports IPv6 socket option variant for an IPv6 address,
// SocketOptionImpl::setIpSocketOption() works.
TEST_F(SocketOptionImplTest, V6Only) {
  Address::Ipv6Instance address("::1:2:3:4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  const int option = 42;
  EXPECT_CALL(os_sys_calls_, setsockopt_(fd, IPPROTO_IPV6, 456, &option, sizeof(int)));
  EXPECT_EQ(0, SocketOptionImpl::setIpSocketOption(socket_, {}, {456}, &option, sizeof(option)));
}

// If a platform suppports only the IPv4 variant for an IPv6 address,
// SocketOptionImpl::setIpSocketOption() works with the IPv4 variant.
TEST_F(SocketOptionImplTest, V6OnlyV4Fallback) {
  Address::Ipv6Instance address("::1:2:3:4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  const int option = 42;
  EXPECT_CALL(os_sys_calls_, setsockopt_(fd, IPPROTO_IP, 123, &option, sizeof(int)));
  EXPECT_EQ(0, SocketOptionImpl::setIpSocketOption(socket_, {123}, {}, &option, sizeof(option)));
}

// If a platform suppports IPv4 and IPv6 socket option variants for an IPv6 address,
// SocketOptionImpl::setIpSocketOption() works with the IPv6 variant.
TEST_F(SocketOptionImplTest, V6Precedence) {
  Address::Ipv6Instance address("::1:2:3:4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  const int option = 42;
  EXPECT_CALL(os_sys_calls_, setsockopt_(fd, IPPROTO_IPV6, 456, &option, sizeof(int)));
  EXPECT_EQ(0, SocketOptionImpl::setIpSocketOption(socket_, {123}, {456}, &option, sizeof(option)));
}

} // namespace
} // namespace Network
} // namespace Envoy
