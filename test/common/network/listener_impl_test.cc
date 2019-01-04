#include <memory>
#include <string>

#include "common/network/address_impl.h"
#include "common/network/listener_impl.h"
#include "common/network/udp_listener_impl.h"
#include "common/network/utility.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Network {

static void errorCallbackTest(Address::IpVersion version) {
  // Force the error callback to fire by closing the socket under the listener. We run this entire
  // test in the forked process to avoid confusion when the fork happens.
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  DangerousDeprecatedTestTime test_time;
  Event::DispatcherImpl dispatcher(test_time.timeSystem(), *api);

  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(version), nullptr,
                                  true);
  Network::MockListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener =
      dispatcher.createListener(socket, listener_callbacks, true, false);

  Network::ClientConnectionPtr client_connection = dispatcher.createClientConnection(
      socket.localAddress(), Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket(), nullptr);
  client_connection->connect();

  EXPECT_CALL(listener_callbacks, onAccept_(_, _))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket, bool) -> void {
        Network::ConnectionPtr new_connection = dispatcher.createServerConnection(
            std::move(socket), Network::Test::createRawBufferSocket());
        listener_callbacks.onNewConnection(std::move(new_connection));
      }));
  EXPECT_CALL(listener_callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        socket.close();
      }));

  dispatcher.run(Event::Dispatcher::RunType::Block);
}

class ListenerImplDeathTest : public testing::TestWithParam<Address::IpVersion> {};
INSTANTIATE_TEST_CASE_P(IpVersions, ListenerImplDeathTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);
TEST_P(ListenerImplDeathTest, ErrorCallback) {
  EXPECT_DEATH_LOG_TO_STDERR(errorCallbackTest(GetParam()), ".*listener accept failure.*");
}

class TestListenerImpl : public ListenerImpl {
public:
  TestListenerImpl(Event::DispatcherImpl& dispatcher, Socket& socket, ListenerCallbacks& cb,
                   bool bind_to_port, bool hand_off_restored_destination_connections)
      : ListenerImpl(dispatcher, socket, cb, bind_to_port,
                     hand_off_restored_destination_connections) {}

  MOCK_METHOD1(getLocalAddress, Address::InstanceConstSharedPtr(int fd));
};

class TestUdpListenerImpl : public UdpListenerImpl {
public:
  TestUdpListenerImpl(Event::DispatcherImpl& dispatcher, Socket& socket, UdpListenerCallbacks& cb,
                      bool bind_to_port)
      : UdpListenerImpl(dispatcher, socket, cb, bind_to_port) {}

  MOCK_METHOD1(getLocalAddress, Address::InstanceConstSharedPtr(int fd));
};

class ListenerImplTest : public testing::TestWithParam<Address::IpVersion> {
protected:
  ListenerImplTest()
      : version_(GetParam()),
        alt_address_(Network::Test::findOrCheckFreePort(
            Network::Test::getCanonicalLoopbackAddress(version_), Address::SocketType::Stream)),
        api_(Api::createApiForTest(stats_store_)), dispatcher_(test_time_.timeSystem(), *api_) {}

  SocketPtr getSocket(Address::SocketType type, const Address::InstanceConstSharedPtr& address,
                      const Network::Socket::OptionsSharedPtr& options, bool bind) {
    if (type == Address::SocketType::Stream) {
      using NetworkSocketTraitType = NetworkSocketTrait<Address::SocketType::Stream>;
      return std::make_unique<NetworkListenSocket<NetworkSocketTraitType>>(address, options, bind);
    } else if (type == Address::SocketType::Datagram) {
      using NetworkSocketTraitType = NetworkSocketTrait<Address::SocketType::Datagram>;
      return std::make_unique<NetworkListenSocket<NetworkSocketTraitType>>(address, options, bind);
    }

    return nullptr;
  }

  // TODO(conqerAtapple): Move this to a common place(address.h?)
  void getSocketAddressInfo(const Socket& socket, sockaddr_storage& addr, socklen_t& sz) {
    memset(&addr, 0, sizeof(addr));

    auto const* ip = socket.localAddress()->ip();
    if (!ip) {
      sz = 0;
      return;
    }

    if (version_ == Address::IpVersion::v4) {
      addr.ss_family = AF_INET;
      auto const* ipv4 = ip->ipv4();
      if (!ipv4) {
        sz = 0;
        return;
      }

      sockaddr_in* addrv4 = reinterpret_cast<sockaddr_in*>(&addr);
      addrv4->sin_port = htons(ip->port());
      addrv4->sin_addr.s_addr = ipv4->address();

      sz = sizeof(sockaddr_in);
    } else if (version_ == Address::IpVersion::v6) {
      addr.ss_family = AF_INET6;
      auto const* ipv6 = ip->ipv6();
      if (!ipv6) {
        sz = 0;
        return;
      }

      sockaddr_in6* addrv6 = reinterpret_cast<sockaddr_in6*>(&addr);
      addrv6->sin6_port = htons(ip->port());

      const auto address = ipv6->address();
      memcpy(static_cast<void*>(&addrv6->sin6_addr.s6_addr), static_cast<const void*>(&address),
             sizeof(absl::uint128));

      sz = sizeof(sockaddr_in6);
    } else {
      sz = 0;
    }
  }

  const Address::IpVersion version_;
  const Address::InstanceConstSharedPtr alt_address_;
  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  DangerousDeprecatedTestTime test_time_;
  Event::DispatcherImpl dispatcher_;
};
INSTANTIATE_TEST_CASE_P(IpVersions, ListenerImplTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

// Test that socket options are set after the listener is setup.
TEST_P(ListenerImplTest, SetListeningSocketOptionsSuccess) {
  Network::MockListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;

  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(version_), nullptr,
                                  true);
  std::shared_ptr<MockSocketOption> option = std::make_shared<MockSocketOption>();
  socket.addOption(option);
  EXPECT_CALL(*option, setOption(_, envoy::api::v2::core::SocketOption::STATE_LISTENING))
      .WillOnce(Return(true));
  TestListenerImpl listener(dispatcher_, socket, listener_callbacks, true, false);
}

// Test that socket options are set after the listener is setup.
TEST_P(ListenerImplTest, UdpSetListeningSocketOptionsSuccess) {
  Network::MockListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;

  Network::UdpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(version_), nullptr,
                                  true);
  std::shared_ptr<MockSocketOption> option = std::make_shared<MockSocketOption>();
  socket.addOption(option);
}

// Test that an exception is thrown if there is an error setting socket options.
TEST_P(ListenerImplTest, SetListeningSocketOptionsError) {
  Network::MockListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;

  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(version_), nullptr,
                                  true);
  std::shared_ptr<MockSocketOption> option = std::make_shared<MockSocketOption>();
  socket.addOption(option);
  EXPECT_CALL(*option, setOption(_, envoy::api::v2::core::SocketOption::STATE_LISTENING))
      .WillOnce(Return(false));
  EXPECT_THROW_WITH_MESSAGE(TestListenerImpl(dispatcher_, socket, listener_callbacks, true, false),
                            CreateListenerException,
                            fmt::format("cannot set post-listen socket option on socket: {}",
                                        socket.localAddress()->asString()));
}

TEST_P(ListenerImplTest, UseActualDstTcp) {
  Stats::IsolatedStoreImpl stats_store;
  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(version_), nullptr,
                                  true);
  Network::TcpListenSocket socketDst(alt_address_, nullptr, false);
  Network::MockListenerCallbacks listener_callbacks1;
  Network::MockConnectionHandler connection_handler;
  // Do not redirect since use_original_dst is false.
  Network::TestListenerImpl listener(dispatcher_, socket, listener_callbacks1, true, true);
  Network::MockListenerCallbacks listener_callbacks2;
  Network::TestListenerImpl listenerDst(dispatcher_, socketDst, listener_callbacks2, false, false);

  Network::ClientConnectionPtr client_connection = dispatcher_.createClientConnection(
      socket.localAddress(), Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket(), nullptr);
  client_connection->connect();

  EXPECT_CALL(listener, getLocalAddress(_)).Times(0);

  EXPECT_CALL(listener_callbacks2, onAccept_(_, _)).Times(0);
  EXPECT_CALL(listener_callbacks1, onAccept_(_, _))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket, bool) -> void {
        Network::ConnectionPtr new_connection = dispatcher_.createServerConnection(
            std::move(socket), Network::Test::createRawBufferSocket());
        listener_callbacks1.onNewConnection(std::move(new_connection));
      }));
  EXPECT_CALL(listener_callbacks1, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        EXPECT_EQ(*conn->localAddress(), *socket.localAddress());
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        dispatcher_.exit();
      }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
}

TEST_P(ListenerImplTest, UseActualDstUdp) {
  SocketPtr server_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, true);

  ASSERT_NE(server_socket, nullptr);

  auto const* server_ip = server_socket->localAddress()->ip();
  ASSERT_NE(server_ip, nullptr);

  Network::MockUdpListenerCallbacks listener_callbacks;
  Network::TestUdpListenerImpl listener(dispatcher_, *server_socket.get(), listener_callbacks,
                                        true);

  SocketPtr client_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, false);

  const int client_sockfd = client_socket->fd();
  sockaddr_storage server_addr;
  socklen_t addr_len;

  getSocketAddressInfo(*client_socket.get(), server_addr, addr_len);
  ASSERT_GT(addr_len, 0);

  if (version_ == Address::IpVersion::v4) {
    struct sockaddr_in* servaddr = reinterpret_cast<struct sockaddr_in*>(&server_addr);
    servaddr->sin_port = htons(server_ip->port());
  } else if (version_ == Address::IpVersion::v6) {
    struct sockaddr_in6* servaddr = reinterpret_cast<struct sockaddr_in6*>(&server_addr);
    servaddr->sin6_port = htons(server_ip->port());
  }

  const std::string first("first");
  const std::string second("second");

  auto send_rc = ::sendto(client_sockfd, first.c_str(), first.length(), 0,
                          reinterpret_cast<const struct sockaddr*>(&server_addr), addr_len);

  ASSERT_EQ(send_rc, first.length());

  send_rc = ::sendto(client_sockfd, second.c_str(), second.length(), 0,
                     reinterpret_cast<const struct sockaddr*>(&server_addr), addr_len);

  ASSERT_EQ(send_rc, second.length());

  EXPECT_CALL(listener_callbacks, onNewConnection_(_, _, _))
      .WillOnce(
          Invoke([&](Address::InstanceConstSharedPtr local_address,
                     Address::InstanceConstSharedPtr peer_address, Buffer::Instance* data) -> void {
            ASSERT_NE(local_address, nullptr);

            ASSERT_NE(peer_address, nullptr);
            ASSERT_NE(peer_address->ip(), nullptr);

            ASSERT_EQ(local_address->asString(), server_socket->localAddress()->asString());

            ASSERT_EQ(peer_address->ip()->addressAsString(),
                      client_socket->localAddress()->ip()->addressAsString());

            EXPECT_EQ(*local_address, *server_socket->localAddress());
            ASSERT_EQ(data->toString(), first);
          }));

  EXPECT_CALL(listener_callbacks, onData_(_, _, _))
      .WillOnce(
          Invoke([&](Address::InstanceConstSharedPtr local_address,
                     Address::InstanceConstSharedPtr peer_address, Buffer::Instance* data) -> void {
            ASSERT_NE(local_address, nullptr);

            ASSERT_NE(peer_address, nullptr);
            ASSERT_NE(peer_address->ip(), nullptr);

            ASSERT_EQ(local_address->asString(), server_socket->localAddress()->asString());

            ASSERT_EQ(peer_address->ip()->addressAsString(),
                      client_socket->localAddress()->ip()->addressAsString());

            EXPECT_EQ(*local_address, *server_socket->localAddress());
            ASSERT_EQ(data->toString(), second);

            dispatcher_.exit();
          }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
}

TEST_P(ListenerImplTest, WildcardListenerUseActualDst) {
  Stats::IsolatedStoreImpl stats_store;
  Network::TcpListenSocket socket(Network::Test::getAnyAddress(version_), nullptr, true);
  Network::MockListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;
  // Do not redirect since use_original_dst is false.
  Network::TestListenerImpl listener(dispatcher_, socket, listener_callbacks, true, true);

  auto local_dst_address = Network::Utility::getAddressWithPort(
      *Network::Test::getCanonicalLoopbackAddress(version_), socket.localAddress()->ip()->port());
  Network::ClientConnectionPtr client_connection = dispatcher_.createClientConnection(
      local_dst_address, Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket(), nullptr);
  client_connection->connect();

  EXPECT_CALL(listener, getLocalAddress(_)).WillOnce(Return(local_dst_address));

  EXPECT_CALL(listener_callbacks, onAccept_(_, _))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket, bool) -> void {
        Network::ConnectionPtr new_connection = dispatcher_.createServerConnection(
            std::move(socket), Network::Test::createRawBufferSocket());
        listener_callbacks.onNewConnection(std::move(new_connection));
      }));
  EXPECT_CALL(listener_callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        EXPECT_EQ(*conn->localAddress(), *local_dst_address);
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        dispatcher_.exit();
      }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
}

// Test for the correct behavior when a listener is configured with an ANY address that allows
// receiving IPv4 connections on an IPv6 socket. In this case the address instances of both
// local and remote addresses of the connection should be IPv4 instances, as the connection really
// is an IPv4 connection.
TEST_P(ListenerImplTest, WildcardListenerIpv4Compat) {
  Stats::IsolatedStoreImpl stats_store;
  auto option = std::make_unique<MockSocketOption>();
  auto options = std::make_shared<std::vector<Network::Socket::OptionConstSharedPtr>>();
  EXPECT_CALL(*option, setOption(_, envoy::api::v2::core::SocketOption::STATE_PREBIND))
      .WillOnce(Return(true));
  options->emplace_back(std::move(option));

  Network::TcpListenSocket socket(Network::Test::getAnyAddress(version_, true), options, true);
  Network::MockListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;

  ASSERT_TRUE(socket.localAddress()->ip()->isAnyAddress());

  // Do not redirect since use_original_dst is false.
  Network::TestListenerImpl listener(dispatcher_, socket, listener_callbacks, true, true);

  auto listener_address = Network::Utility::getAddressWithPort(
      *Network::Test::getCanonicalLoopbackAddress(version_), socket.localAddress()->ip()->port());
  auto local_dst_address = Network::Utility::getAddressWithPort(
      *Network::Utility::getCanonicalIpv4LoopbackAddress(), socket.localAddress()->ip()->port());
  Network::ClientConnectionPtr client_connection = dispatcher_.createClientConnection(
      local_dst_address, Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket(), nullptr);
  client_connection->connect();

  EXPECT_CALL(listener, getLocalAddress(_))
      .WillOnce(Invoke(
          [](int fd) -> Address::InstanceConstSharedPtr { return Address::addressFromFd(fd); }));

  EXPECT_CALL(listener_callbacks, onAccept_(_, _))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket, bool) -> void {
        Network::ConnectionPtr new_connection = dispatcher_.createServerConnection(
            std::move(socket), Network::Test::createRawBufferSocket());
        listener_callbacks.onNewConnection(std::move(new_connection));
      }));
  EXPECT_CALL(listener_callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        EXPECT_EQ(conn->localAddress()->ip()->version(), conn->remoteAddress()->ip()->version());
        EXPECT_EQ(conn->localAddress()->asString(), local_dst_address->asString());
        EXPECT_EQ(*conn->localAddress(), *local_dst_address);
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        dispatcher_.exit();
      }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
}

TEST_P(ListenerImplTest, DisableAndEnableListener) {
  testing::InSequence s1;

  TcpListenSocket socket(Network::Test::getAnyAddress(version_), nullptr, true);
  MockListenerCallbacks listener_callbacks;
  TestListenerImpl listener(dispatcher_, socket, listener_callbacks, true, true);

  // When listener is disabled, the timer should fire before any connection is accepted.
  listener.disable();

  ClientConnectionPtr client_connection =
      dispatcher_.createClientConnection(socket.localAddress(), Address::InstanceConstSharedPtr(),
                                         Network::Test::createRawBufferSocket(), nullptr);
  client_connection->connect();
  Event::TimerPtr timer = dispatcher_.createTimer([&] {
    client_connection->close(ConnectionCloseType::NoFlush);
    dispatcher_.exit();
  });
  timer->enableTimer(std::chrono::milliseconds(2000));

  EXPECT_CALL(listener_callbacks, onAccept_(_, _)).Times(0);

  dispatcher_.run(Event::Dispatcher::RunType::Block);

  // When the listener is re-enabled, the pending connection should be accepted.
  listener.enable();

  EXPECT_CALL(listener, getLocalAddress(_))
      .WillOnce(Invoke(
          [](int fd) -> Address::InstanceConstSharedPtr { return Address::addressFromFd(fd); }));
  EXPECT_CALL(listener_callbacks, onAccept_(_, _))
      .WillOnce(Invoke([&](ConnectionSocketPtr&, bool) -> void {
        client_connection->close(ConnectionCloseType::NoFlush);
        dispatcher_.exit();
      }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
}

} // namespace Network
} // namespace Envoy
