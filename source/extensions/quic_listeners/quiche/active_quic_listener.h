#pragma once

#include "envoy/config/listener/v3/quic_config.pb.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/listener.h"
#include "envoy/runtime/runtime.h"

#include "common/protobuf/utility.h"
#include "common/runtime/runtime_protos.h"

#include "server/connection_handler_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_dispatcher.h"

namespace Envoy {
namespace Quic {

// QUIC specific UdpListenerCallbacks implementation which delegates incoming
// packets, write signals and listener errors to QuicDispatcher.
class ActiveQuicListener : public Network::UdpListenerCallbacks,
                           public Server::ConnectionHandlerImpl::ActiveListenerImplBase,
                           Logger::Loggable<Logger::Id::quic> {
public:
  // TODO(bencebeky): Tune this value.
  static const size_t kNumSessionsToCreatePerLoop = 16;

  ActiveQuicListener(Event::Dispatcher& dispatcher, Network::ConnectionHandler& parent,
                     Network::ListenerConfig& listener_config, const quic::QuicConfig& quic_config,
                     Runtime::Loader& runtime);

  ActiveQuicListener(Event::Dispatcher& dispatcher, Network::ConnectionHandler& parent,
                     Network::SocketSharedPtr listen_socket,
                     Network::ListenerConfig& listener_config, const quic::QuicConfig& quic_config,
                     Runtime::Loader& runtime);

  ~ActiveQuicListener() override;

  // TODO(#7465): Make this a callback.
  void onListenerShutdown();

  // Network::UdpListenerCallbacks
  void onData(Network::UdpRecvData& data) override;
  void onReadReady() override;
  void onWriteReady(const Network::Socket& socket) override;
  void onReceiveError(Api::IoError::IoErrorCode /*error_code*/) override {
    // No-op. Quic can't do anything upon listener error.
  }

  // ActiveListenerImplBase
  Network::Listener* listener() override { return udp_listener_.get(); }
  void destroy() override { udp_listener_.reset(); }

  bool enabled() { return enabled_.enabled(); }

private:
  friend class ActiveQuicListenerPeer;
  Network::UdpListenerPtr udp_listener_;
  uint8_t random_seed_[16];
  std::unique_ptr<quic::QuicCryptoServerConfig> crypto_config_;
  Event::Dispatcher& dispatcher_;
  quic::QuicVersionManager version_manager_;
  std::unique_ptr<EnvoyQuicDispatcher> quic_dispatcher_;
  Network::Socket& listen_socket_;
  Runtime::FeatureFlag enabled_;
};

using ActiveQuicListenerPtr = std::unique_ptr<ActiveQuicListener>;

// A factory to create ActiveQuicListener based on given config.
class ActiveQuicListenerFactory : public Network::ActiveUdpListenerFactory {
public:
  ActiveQuicListenerFactory(const envoy::config::listener::v3::QuicProtocolOptions& config,
                            const Runtime::Loader& runtime) {
    uint64_t idle_network_timeout_ms =
        config.has_idle_timeout() ? DurationUtil::durationToMilliseconds(config.idle_timeout())
                                  : 300000;
    quic_config_.SetIdleNetworkTimeout(
        quic::QuicTime::Delta::FromMilliseconds(idle_network_timeout_ms),
        quic::QuicTime::Delta::FromMilliseconds(idle_network_timeout_ms));
    int32_t max_time_before_crypto_handshake_ms =
        config.has_crypto_handshake_timeout()
            ? DurationUtil::durationToMilliseconds(config.crypto_handshake_timeout())
            : 20000;
    quic_config_.set_max_time_before_crypto_handshake(
        quic::QuicTime::Delta::FromMilliseconds(max_time_before_crypto_handshake_ms));
    int32_t max_streams = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_concurrent_streams, 100);
    quic_config_.SetMaxIncomingBidirectionalStreamsToSend(max_streams);
    quic_config_.SetMaxIncomingUnidirectionalStreamsToSend(max_streams);
    runtime_(runtime);
  }

  // Network::ActiveUdpListenerFactory.
  Network::ConnectionHandler::ActiveListenerPtr
  createActiveUdpListener(Network::ConnectionHandler& parent, Event::Dispatcher& dispatcher,
                          Network::ListenerConfig& config) const override {
    return std::make_unique<ActiveQuicListener>(dispatcher, parent, config, quic_config_, runtime_);
  }
  bool isTransportConnectionless() const override { return false; }

private:
  friend class ActiveQuicListenerFactoryPeer;

  quic::QuicConfig quic_config_;
  Runtime::Loader& runtime_;
};

} // namespace Quic
} // namespace Envoy
