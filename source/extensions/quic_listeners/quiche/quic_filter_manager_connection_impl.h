#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"

#include "common/common/empty_string.h"
#include "common/common/logger.h"
#include "common/network/filter_manager_impl.h"
#include "common/stream_info/stream_info_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_connection.h"
#include "extensions/quic_listeners/quiche/envoy_quic_simulated_watermark_buffer.h"

namespace Envoy {
namespace Quic {

// Act as a Network::Connection to HCM and a FilterManager to FilterFactoryCb.
class QuicFilterManagerConnectionImpl : public Network::FilterManagerConnection,
                                        protected Logger::Loggable<Logger::Id::connection> {
public:
  QuicFilterManagerConnectionImpl(EnvoyQuicConnection& connection, Event::Dispatcher& dispatcher,
                                  uint32_t send_buffer_limit);

  // Network::FilterManager
  // Overridden to delegate calls to filter_manager_.
  void addWriteFilter(Network::WriteFilterSharedPtr filter) override;
  void addFilter(Network::FilterSharedPtr filter) override;
  void addReadFilter(Network::ReadFilterSharedPtr filter) override;
  bool initializeReadFilters() override;

  // Network::Connection
  void addConnectionCallbacks(Network::ConnectionCallbacks& cb) override;
  void addBytesSentCallback(Network::Connection::BytesSentCb /*cb*/) override {
    // TODO(danzh): implement to support proxy. This interface is only called from
    // TCP proxy code.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  void enableHalfClose(bool enabled) override;
  void close(Network::ConnectionCloseType type) override;
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  uint64_t id() const override { return id_; }
  std::string nextProtocol() const override { return EMPTY_STRING; }
  void noDelay(bool /*enable*/) override {
    // No-op. TCP_NODELAY doesn't apply to UDP.
  }
  void setDelayedCloseTimeout(std::chrono::milliseconds timeout) override;
  std::chrono::milliseconds delayedCloseTimeout() const override;
  void readDisable(bool /*disable*/) override { NOT_REACHED_GCOVR_EXCL_LINE; }
  void detectEarlyCloseWhenReadDisabled(bool /*value*/) override { NOT_REACHED_GCOVR_EXCL_LINE; }
  bool readEnabled() const override { return true; }
  const Network::Address::InstanceConstSharedPtr& remoteAddress() const override;
  const Network::Address::InstanceConstSharedPtr& localAddress() const override;
  absl::optional<Network::Connection::UnixDomainSocketPeerCredentials>
  unixSocketPeerCredentials() const override {
    // Unix domain socket is not supported.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  void setConnectionStats(const Network::Connection::ConnectionStats& stats) override {
    stats_ = std::make_unique<Network::Connection::ConnectionStats>(stats);
    quic_connection_->setConnectionStats(stats);
  }
  Ssl::ConnectionInfoConstSharedPtr ssl() const override;
  Network::Connection::State state() const override {
    if (quic_connection_ != nullptr && quic_connection_->connected()) {
      return Network::Connection::State::Open;
    }
    return Network::Connection::State::Closed;
  }
  void write(Buffer::Instance& /*data*/, bool /*end_stream*/) override {
    // All writes should be handled by Quic internally.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  void setBufferLimits(uint32_t limit) override;
  uint32_t bufferLimit() const override {
    // As quic connection is not HTTP1.1, this method shouldn't be called by HCM.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  bool localAddressRestored() const override {
    // SO_ORIGINAL_DST not supported by QUIC.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  bool aboveHighWatermark() const override;

  const Network::ConnectionSocket::OptionsSharedPtr& socketOptions() const override;
  StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }
  const StreamInfo::StreamInfo& streamInfo() const override { return stream_info_; }
  absl::string_view transportFailureReason() const override { return transport_failure_reason_; }

  // Network::FilterManagerConnection
  void rawWrite(Buffer::Instance& data, bool end_stream) override;

  // Network::ReadBufferSource
  Network::StreamBuffer getReadBuffer() override {
    // Network filter has to stop iteration to prevent hitting this line.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  // Network::WriteBufferSource
  Network::StreamBuffer getWriteBuffer() override { NOT_REACHED_GCOVR_EXCL_LINE; }

  void adjustBytesToSend(int64_t delta);

protected:
  // Propagate connection close to network_connection_callbacks_.
  void onConnectionCloseEvent(const quic::QuicConnectionCloseFrame& frame,
                              quic::ConnectionCloseSource source);

  void raiseEvent(Network::ConnectionEvent event);

  EnvoyQuicConnection* quic_connection_{nullptr};
  // TODO(danzh): populate stats.
  std::unique_ptr<Network::Connection::ConnectionStats> stats_;
  Event::Dispatcher& dispatcher_;

private:
  // Called when aggregated buffered bytes across all the streams exceeds high watermark.
  void onSendBufferHighWatermark();
  // Called when aggregated buffered bytes across all the streams declines to low watermark.
  void onSendBufferLowWatermark();

  // Currently ConnectionManagerImpl is the one and only filter. If more network
  // filters are added, ConnectionManagerImpl should always be the last one.
  // Its onRead() is only called once to trigger ReadFilter::onNewConnection()
  // and the rest incoming data bypasses these filters.
  Network::FilterManagerImpl filter_manager_;
  StreamInfo::StreamInfoImpl stream_info_;
  // These callbacks are owned by network filters and quic session should out live
  // them.
  std::list<Network::ConnectionCallbacks*> network_connection_callbacks_;
  std::string transport_failure_reason_;
  const uint64_t id_;
  uint32_t bytes_to_send_{0};
  EnvoyQuicSimulatedWatermarkBuffer write_buffer_watermark_simulation_;
};

} // namespace Quic
} // namespace Envoy
