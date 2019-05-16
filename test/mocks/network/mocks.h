#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "envoy/api/v2/core/address.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/network/resolver.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stats/scope.h"

#include "common/network/filter_manager_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Network {

class MockActiveDnsQuery : public ActiveDnsQuery {
public:
  MockActiveDnsQuery();
  ~MockActiveDnsQuery();

  // Network::ActiveDnsQuery
  MOCK_METHOD0(cancel, void());
};

class MockDnsResolver : public DnsResolver {
public:
  MockDnsResolver();
  ~MockDnsResolver();

  // Network::DnsResolver
  MOCK_METHOD3(resolve, ActiveDnsQuery*(const std::string& dns_name,
                                        DnsLookupFamily dns_lookup_family, ResolveCb callback));

  testing::NiceMock<MockActiveDnsQuery> active_query_;
};

class MockAddressResolver : public Address::Resolver {
public:
  MockAddressResolver();
  ~MockAddressResolver();

  MOCK_METHOD1(resolve,
               Address::InstanceConstSharedPtr(const envoy::api::v2::core::SocketAddress&));
  MOCK_CONST_METHOD0(name, std::string());
};

class MockReadFilterCallbacks : public ReadFilterCallbacks {
public:
  MockReadFilterCallbacks();
  ~MockReadFilterCallbacks();

  MOCK_METHOD0(connection, Connection&());
  MOCK_METHOD0(continueReading, void());
  MOCK_METHOD2(injectReadDataToFilterChain, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD0(upstreamHost, Upstream::HostDescriptionConstSharedPtr());
  MOCK_METHOD1(upstreamHost, void(Upstream::HostDescriptionConstSharedPtr host));

  testing::NiceMock<MockConnection> connection_;
  Upstream::HostDescriptionConstSharedPtr host_;
};

class MockReadFilter : public ReadFilter {
public:
  MockReadFilter();
  ~MockReadFilter();

  MOCK_METHOD2(onData, FilterStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD0(onNewConnection, FilterStatus());
  MOCK_METHOD1(initializeReadFilterCallbacks, void(ReadFilterCallbacks& callbacks));

  ReadFilterCallbacks* callbacks_{};
};

class MockWriteFilterCallbacks : public WriteFilterCallbacks {
public:
  MockWriteFilterCallbacks();
  ~MockWriteFilterCallbacks();

  MOCK_METHOD0(connection, Connection&());
  MOCK_METHOD2(injectWriteDataToFilterChain, void(Buffer::Instance& data, bool end_stream));

  testing::NiceMock<MockConnection> connection_;
};

class MockWriteFilter : public WriteFilter {
public:
  MockWriteFilter();
  ~MockWriteFilter();

  MOCK_METHOD2(onWrite, FilterStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(initializeWriteFilterCallbacks, void(WriteFilterCallbacks& callbacks));

  WriteFilterCallbacks* write_callbacks_{};
};

class MockFilter : public Filter {
public:
  MockFilter();
  ~MockFilter();

  MOCK_METHOD2(onData, FilterStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD0(onNewConnection, FilterStatus());
  MOCK_METHOD2(onWrite, FilterStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(initializeReadFilterCallbacks, void(ReadFilterCallbacks& callbacks));
  MOCK_METHOD1(initializeWriteFilterCallbacks, void(WriteFilterCallbacks& callbacks));

  ReadFilterCallbacks* callbacks_{};
  WriteFilterCallbacks* write_callbacks_{};
};

class MockListenerCallbacks : public ListenerCallbacks {
public:
  MockListenerCallbacks();
  ~MockListenerCallbacks();

  void onAccept(ConnectionSocketPtr&& socket, bool redirected) override {
    onAccept_(socket, redirected);
  }
  void onNewConnection(ConnectionPtr&& conn) override { onNewConnection_(conn); }

  MOCK_METHOD2(onAccept_, void(ConnectionSocketPtr& socket, bool redirected));
  MOCK_METHOD1(onNewConnection_, void(ConnectionPtr& conn));
};

class MockUdpListenerCallbacks : public UdpListenerCallbacks {
public:
  MockUdpListenerCallbacks();
  ~MockUdpListenerCallbacks();

  void onData(const UdpData& data) override { onData_(data); }

  void onWriteReady(const Socket& socket) override { onWriteReady_(socket); }

  void onError(const ErrorCode& err_code, int err) override { onError_(err_code, err); }

  MOCK_METHOD1(onData_, void(const UdpData& data));

  MOCK_METHOD1(onWriteReady_, void(const Socket& socket));

  MOCK_METHOD2(onError_, void(const ErrorCode& err_code, int err));
};

class MockDrainDecision : public DrainDecision {
public:
  MockDrainDecision();
  ~MockDrainDecision();

  MOCK_CONST_METHOD0(drainClose, bool());
};

class MockListenerFilter : public Network::ListenerFilter {
public:
  MockListenerFilter();
  ~MockListenerFilter();

  MOCK_METHOD1(onAccept, Network::FilterStatus(Network::ListenerFilterCallbacks&));
};

class MockListenerFilterManager : public ListenerFilterManager {
public:
  MockListenerFilterManager();
  ~MockListenerFilterManager();

  void addAcceptFilter(Network::ListenerFilterPtr&& filter) override { addAcceptFilter_(filter); }

  MOCK_METHOD1(addAcceptFilter_, void(Network::ListenerFilterPtr&));
};

class MockFilterChain : public FilterChain {
public:
  MockFilterChain();
  ~MockFilterChain();

  // Network::FilterChain
  MOCK_CONST_METHOD0(transportSocketFactory, const TransportSocketFactory&());
  MOCK_CONST_METHOD0(networkFilterFactories, const std::vector<FilterFactoryCb>&());
};

class MockFilterChainManager : public FilterChainManager {
public:
  MockFilterChainManager();
  ~MockFilterChainManager();

  // Network::FilterChainManager
  MOCK_CONST_METHOD1(findFilterChain, const FilterChain*(const ConnectionSocket& socket));
};

class MockFilterChainFactory : public FilterChainFactory {
public:
  MockFilterChainFactory();
  ~MockFilterChainFactory();

  MOCK_METHOD2(createNetworkFilterChain,
               bool(Connection& connection,
                    const std::vector<Network::FilterFactoryCb>& filter_factories));
  MOCK_METHOD1(createListenerFilterChain, bool(ListenerFilterManager& listener));
};

class MockListenSocket : public Socket {
public:
  MockListenSocket();
  ~MockListenSocket() override {}

  void addOption(const Socket::OptionConstSharedPtr& option) override { addOption_(option); }
  void addOptions(const Socket::OptionsSharedPtr& options) override { addOptions_(options); }

  MOCK_CONST_METHOD0(localAddress, const Address::InstanceConstSharedPtr&());
  MOCK_METHOD1(setLocalAddress, void(const Address::InstanceConstSharedPtr&));
  MOCK_METHOD0(ioHandle, IoHandle&());
  MOCK_CONST_METHOD0(ioHandle, const IoHandle&());
  MOCK_CONST_METHOD0(socketType, Address::SocketType());
  MOCK_METHOD0(close, void());
  MOCK_METHOD1(addOption_, void(const Socket::OptionConstSharedPtr& option));
  MOCK_METHOD1(addOptions_, void(const Socket::OptionsSharedPtr& options));
  MOCK_CONST_METHOD0(options, const OptionsSharedPtr&());

  IoHandlePtr io_handle_;
  Address::InstanceConstSharedPtr local_address_;
  OptionsSharedPtr options_;
};

class MockSocketOption : public Socket::Option {
public:
  MockSocketOption();
  ~MockSocketOption();

  MOCK_CONST_METHOD2(setOption,
                     bool(Socket&, envoy::api::v2::core::SocketOption::SocketState state));
  MOCK_CONST_METHOD1(hashKey, void(std::vector<uint8_t>&));
  MOCK_CONST_METHOD2(getOptionDetails,
                     absl::optional<Socket::Option::Details>(
                         const Socket&, envoy::api::v2::core::SocketOption::SocketState state));
};

class MockConnectionSocket : public ConnectionSocket {
public:
  MockConnectionSocket();
  ~MockConnectionSocket() override {}

  void addOption(const Socket::OptionConstSharedPtr& option) override { addOption_(option); }
  void addOptions(const Socket::OptionsSharedPtr& options) override { addOptions_(options); }

  MOCK_CONST_METHOD0(localAddress, const Address::InstanceConstSharedPtr&());
  MOCK_METHOD1(setLocalAddress, void(const Address::InstanceConstSharedPtr&));
  MOCK_METHOD1(restoreLocalAddress, void(const Address::InstanceConstSharedPtr&));
  MOCK_CONST_METHOD0(localAddressRestored, bool());
  MOCK_METHOD1(setRemoteAddress, void(const Address::InstanceConstSharedPtr&));
  MOCK_CONST_METHOD0(remoteAddress, const Address::InstanceConstSharedPtr&());
  MOCK_METHOD1(setDetectedTransportProtocol, void(absl::string_view));
  MOCK_CONST_METHOD0(detectedTransportProtocol, absl::string_view());
  MOCK_METHOD1(setRequestedApplicationProtocols, void(const std::vector<absl::string_view>&));
  MOCK_CONST_METHOD0(requestedApplicationProtocols, const std::vector<std::string>&());
  MOCK_METHOD1(setRequestedServerName, void(absl::string_view));
  MOCK_CONST_METHOD0(requestedServerName, absl::string_view());
  MOCK_METHOD1(addOption_, void(const Socket::OptionConstSharedPtr&));
  MOCK_METHOD1(addOptions_, void(const Socket::OptionsSharedPtr&));
  MOCK_CONST_METHOD0(options, const Network::ConnectionSocket::OptionsSharedPtr&());
  MOCK_METHOD0(ioHandle, IoHandle&());
  MOCK_CONST_METHOD0(ioHandle, const IoHandle&());
  MOCK_CONST_METHOD0(socketType, Address::SocketType());
  MOCK_METHOD0(close, void());

  IoHandlePtr io_handle_;
  Address::InstanceConstSharedPtr local_address_;
  Address::InstanceConstSharedPtr remote_address_;
};

class MockListenerFilterCallbacks : public ListenerFilterCallbacks {
public:
  MockListenerFilterCallbacks();
  ~MockListenerFilterCallbacks();

  MOCK_METHOD0(socket, ConnectionSocket&());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD1(continueFilterChain, void(bool));

  NiceMock<MockConnectionSocket> socket_;
};

class MockListenerConfig : public ListenerConfig {
public:
  MockListenerConfig();
  ~MockListenerConfig();

  MOCK_METHOD0(filterChainManager, FilterChainManager&());
  MOCK_METHOD0(filterChainFactory, FilterChainFactory&());
  MOCK_METHOD0(socket, Socket&());
  MOCK_CONST_METHOD0(socket, const Socket&());
  MOCK_METHOD0(bindToPort, bool());
  MOCK_CONST_METHOD0(handOffRestoredDestinationConnections, bool());
  MOCK_CONST_METHOD0(perConnectionBufferLimitBytes, uint32_t());
  MOCK_CONST_METHOD0(listenerFiltersTimeout, std::chrono::milliseconds());
  MOCK_METHOD0(listenerScope, Stats::Scope&());
  MOCK_CONST_METHOD0(listenerTag, uint64_t());
  MOCK_CONST_METHOD0(name, const std::string&());

  testing::NiceMock<MockFilterChainFactory> filter_chain_factory_;
  testing::NiceMock<MockListenSocket> socket_;
  Stats::IsolatedStoreImpl scope_;
  std::string name_;
};

class MockListener : public Listener {
public:
  MockListener();
  ~MockListener();

  MOCK_METHOD0(onDestroy, void());
  MOCK_METHOD0(enable, void());
  MOCK_METHOD0(disable, void());
};

class MockConnectionHandler : public ConnectionHandler {
public:
  MockConnectionHandler();
  ~MockConnectionHandler();

  MOCK_METHOD0(numConnections, uint64_t());
  MOCK_METHOD1(addListener, void(ListenerConfig& config));
  MOCK_METHOD1(addUdpListener, void(ListenerConfig& config));
  MOCK_METHOD1(findListenerByAddress,
               Network::Listener*(const Network::Address::Instance& address));
  MOCK_METHOD1(removeListeners, void(uint64_t listener_tag));
  MOCK_METHOD1(stopListeners, void(uint64_t listener_tag));
  MOCK_METHOD0(stopListeners, void());
  MOCK_METHOD0(disableListeners, void());
  MOCK_METHOD0(enableListeners, void());
};

class MockIp : public Address::Ip {
public:
  MockIp();
  ~MockIp();

  MOCK_CONST_METHOD0(addressAsString, const std::string&());
  MOCK_CONST_METHOD0(isAnyAddress, bool());
  MOCK_CONST_METHOD0(isUnicastAddress, bool());
  MOCK_CONST_METHOD0(ipv4, Address::Ipv4*());
  MOCK_CONST_METHOD0(ipv6, Address::Ipv6*());
  MOCK_CONST_METHOD0(port, uint32_t());
  MOCK_CONST_METHOD0(version, Address::IpVersion());
};

class MockResolvedAddress : public Address::Instance {
public:
  MockResolvedAddress(const std::string& logical, const std::string& physical)
      : logical_(logical), physical_(physical) {}
  ~MockResolvedAddress();

  bool operator==(const Address::Instance& other) const override {
    return asString() == other.asString();
  }

  MOCK_CONST_METHOD1(bind, Api::SysCallIntResult(int));
  MOCK_CONST_METHOD1(connect, Api::SysCallIntResult(int));
  MOCK_CONST_METHOD0(ip, Address::Ip*());
  MOCK_CONST_METHOD1(socket, IoHandlePtr(Address::SocketType));
  MOCK_CONST_METHOD0(type, Address::Type());

  const std::string& asString() const override { return physical_; }
  const std::string& logicalName() const override { return logical_; }
  std::chrono::seconds ttl() const override { return std::chrono::seconds::max(); }

  const std::string logical_;
  const std::string physical_;
};

class MockTransportSocket : public TransportSocket {
public:
  MockTransportSocket();
  ~MockTransportSocket();

  MOCK_METHOD1(setTransportSocketCallbacks, void(TransportSocketCallbacks& callbacks));
  MOCK_CONST_METHOD0(protocol, std::string());
  MOCK_CONST_METHOD0(failureReason, absl::string_view());
  MOCK_METHOD0(canFlushClose, bool());
  MOCK_METHOD1(closeSocket, void(Network::ConnectionEvent event));
  MOCK_METHOD1(doRead, IoResult(Buffer::Instance& buffer));
  MOCK_METHOD2(doWrite, IoResult(Buffer::Instance& buffer, bool end_stream));
  MOCK_METHOD0(onConnected, void());
  MOCK_CONST_METHOD0(ssl, const Ssl::ConnectionInfo*());

  TransportSocketCallbacks* callbacks_{};
};

class MockTransportSocketFactory : public TransportSocketFactory {
public:
  MockTransportSocketFactory();
  ~MockTransportSocketFactory();

  MOCK_CONST_METHOD0(implementsSecureTransport, bool());
  MOCK_CONST_METHOD1(createTransportSocket, TransportSocketPtr(TransportSocketOptionsSharedPtr));
};

class MockTransportSocketCallbacks : public TransportSocketCallbacks {
public:
  MockTransportSocketCallbacks();
  ~MockTransportSocketCallbacks();

  MOCK_METHOD0(ioHandle, IoHandle&());
  MOCK_CONST_METHOD0(ioHandle, const IoHandle&());
  MOCK_METHOD0(connection, Connection&());
  MOCK_METHOD0(shouldDrainReadBuffer, bool());
  MOCK_METHOD0(setReadBufferReady, void());
  MOCK_METHOD1(raiseEvent, void(ConnectionEvent));

  testing::NiceMock<MockConnection> connection_;
};

} // namespace Network
} // namespace Envoy
