#pragma once

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif

#include "quiche/quic/core/http/quic_spdy_client_session.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "common/quic/envoy_quic_client_stream.h"
#include "common/quic/envoy_quic_client_connection.h"
#include "common/quic/quic_filter_manager_connection_impl.h"

namespace Envoy {
namespace Quic {

// Act as a Network::ClientConnection to ClientCodec.
// TODO(danzh) This class doesn't need to inherit Network::FilterManager
// interface but need all other Network::Connection implementation in
// QuicFilterManagerConnectionImpl. Refactor QuicFilterManagerConnectionImpl to
// move FilterManager interface to EnvoyQuicServerSession.
class EnvoyQuicClientSession : public QuicFilterManagerConnectionImpl,
                               public quic::QuicSpdyClientSession,
                               public Network::ClientConnection {
public:
  EnvoyQuicClientSession(const quic::QuicConfig& config,
                         const quic::ParsedQuicVersionVector& supported_versions,
                         std::unique_ptr<EnvoyQuicClientConnection> connection,
                         const quic::QuicServerId& server_id,
                         quic::QuicCryptoClientConfig* crypto_config,
                         quic::QuicClientPushPromiseIndex* push_promise_index,
                         Event::Dispatcher& dispatcher, uint32_t send_buffer_limit);

  ~EnvoyQuicClientSession() override;

  // Called by QuicHttpClientConnectionImpl before creating data streams.
  void setHttpConnectionCallbacks(Http::ConnectionCallbacks& callbacks) {
    http_connection_callbacks_ = &callbacks;
  }

  // Network::Connection
  absl::string_view requestedServerName() const override;

  // Network::ClientConnection
  // Set up socket and start handshake.
  void connect() override;

  // quic::QuicSession
  void OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                          quic::ConnectionCloseSource source) override;
  void Initialize() override;
  void OnCanWrite() override;
  void OnGoAway(const quic::QuicGoAwayFrame& frame) override;
  void OnHttp3GoAway(uint64_t stream_id) override;
  void OnTlsHandshakeComplete() override;
  size_t WriteHeadersOnHeadersStream(
      quic::QuicStreamId id, spdy::SpdyHeaderBlock headers, bool fin,
      const spdy::SpdyStreamPrecedence& precedence,
      quic::QuicReferenceCountedPointer<quic::QuicAckListenerInterface> ack_listener) override;
  // quic::QuicSpdyClientSessionBase
  void SetDefaultEncryptionLevel(quic::EncryptionLevel level) override;

  using quic::QuicSpdyClientSession::PerformActionOnActiveStreams;

protected:
  // quic::QuicSpdyClientSession
  std::unique_ptr<quic::QuicSpdyClientStream> CreateClientStream() override;
  // quic::QuicSpdySession
  quic::QuicSpdyStream* CreateIncomingStream(quic::QuicStreamId id) override;
  quic::QuicSpdyStream* CreateIncomingStream(quic::PendingStream* pending) override;

  // QuicFilterManagerConnectionImpl
  bool hasDataToWrite() override;

private:
  // These callbacks are owned by network filters and quic session should outlive
  // them.
  Http::ConnectionCallbacks* http_connection_callbacks_{nullptr};
  const absl::string_view host_name_;
};

} // namespace Quic
} // namespace Envoy
