#include "common/tcp_proxy/upstream.h"

#include <memory>

#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

namespace Envoy {
namespace TcpProxy {
TcpUpstream::TcpUpstream(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& data,
                         Envoy::Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks)
    : upstream_conn_data_(std::move(data)) {
  Network::ClientConnection& connection = upstream_conn_data_->connection();
  connection.enableHalfClose(true);
  upstream_conn_data_->addUpstreamCallbacks(upstream_callbacks);
}

bool TcpUpstream::readDisable(bool disable) {
  if (upstream_conn_data_ == nullptr ||
      upstream_conn_data_->connection().state() != Network::Connection::State::Open) {
    // Because we flush write downstream, we can have a case where upstream has already disconnected
    // and we are waiting to flush. If we had a watermark event during this time we should no
    // longer touch the upstream connection.
    return false;
  }

  upstream_conn_data_->connection().readDisable(disable);
  return true;
}

void TcpUpstream::encodeData(Buffer::Instance& data, bool end_stream) {
  upstream_conn_data_->connection().write(data, end_stream);
}

void TcpUpstream::addBytesSentCallback(Network::Connection::BytesSentCb cb) {
  upstream_conn_data_->connection().addBytesSentCallback(cb);
}

Envoy::Tcp::ConnectionPool::ConnectionData*
TcpUpstream::onDownstreamEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose) {
    // The close call may result in this object being deleted. Latch the
    // connection locally so it can be returned for potential draining.
    auto* conn_data = upstream_conn_data_.release();
    conn_data->connection().close(Network::ConnectionCloseType::FlushWrite);
    return conn_data;
  } else if (event == Network::ConnectionEvent::LocalClose) {
    upstream_conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
  }
  return nullptr;
}

void TcpConnectionHandle::onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                                      Upstream::HostDescriptionConstSharedPtr host) {
  Network::Connection& latched_conn = conn_data->connection();
  tcp_upstream_ = std::make_shared<TcpUpstream>(std::move(conn_data), tcp_upstream_callbacks_);
  // Disable cancel.
  tcp_upstream_handle_ = nullptr;
  generic_pool_callbacks_.onPoolReady(tcp_upstream_, host, latched_conn.localAddress(),
                                      latched_conn.streamInfo());
}

void TcpConnectionHandle::onPoolFailure(Envoy::Tcp::ConnectionPool::PoolFailureReason reason,
                                        Upstream::HostDescriptionConstSharedPtr host) {
  if (tcp_upstream_handle_ == nullptr) {
    // An immediate failure before creating the connection.
    has_failure_ = true;
  }
  // The onPoolFailure is invoked outside the scope of handle creation. Trigger onPoolFailure.
  generic_pool_callbacks_.onPoolFailure(reason, host);
}

HttpUpstream::HttpUpstream(Envoy::Tcp::ConnectionPool::UpstreamCallbacks& callbacks,
                           const std::string& hostname)
    : upstream_callbacks_(callbacks), response_decoder_(*this), hostname_(hostname) {}

HttpUpstream::~HttpUpstream() { resetEncoder(Network::ConnectionEvent::LocalClose); }

bool HttpUpstream::isValidBytestreamResponse(const Envoy::Http::ResponseHeaderMap& headers) {
  if (Envoy::Http::Utility::getResponseStatus(headers) != 200) {
    return false;
  }
  return true;
}

bool HttpUpstream::readDisable(bool disable) {
  if (!request_encoder_) {
    return false;
  }
  request_encoder_->getStream().readDisable(disable);
  return true;
}

void HttpUpstream::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!request_encoder_) {
    return;
  }
  request_encoder_->encodeData(data, end_stream);
  if (end_stream) {
    doneWriting();
  }
}

void HttpUpstream::addBytesSentCallback(Network::Connection::BytesSentCb) {
  // The HTTP tunneling mode does not tickle the idle timeout when bytes are
  // sent to the kernel.
  // This can be implemented if any user cares about the difference in time
  // between it being sent to the HTTP/2 stack and out to the kernel.
}

Envoy::Tcp::ConnectionPool::ConnectionData*
HttpUpstream::onDownstreamEvent(Network::ConnectionEvent event) {
  if (event != Network::ConnectionEvent::Connected) {
    resetEncoder(Network::ConnectionEvent::LocalClose, false);
  }
  return nullptr;
}

void HttpUpstream::onResetStream(Envoy::Http::StreamResetReason, absl::string_view) {
  read_half_closed_ = true;
  write_half_closed_ = true;
  resetEncoder(Network::ConnectionEvent::LocalClose);
}

void HttpUpstream::onAboveWriteBufferHighWatermark() {
  upstream_callbacks_.onAboveWriteBufferHighWatermark();
}

void HttpUpstream::onBelowWriteBufferLowWatermark() {
  upstream_callbacks_.onBelowWriteBufferLowWatermark();
}

void HttpUpstream::setRequestEncoder(Envoy::Http::RequestEncoder& request_encoder, bool is_ssl) {
  request_encoder_ = &request_encoder;
  request_encoder_->getStream().addCallbacks(*this);
  const std::string& scheme = is_ssl ? Envoy::Http::Headers::get().SchemeValues.Https
                                     : Envoy::Http::Headers::get().SchemeValues.Http;
  auto headers = Envoy::Http::createHeaderMap<Envoy::Http::RequestHeaderMapImpl>(
      {{Envoy::Http::Headers::get().Method, "CONNECT"},
       {Envoy::Http::Headers::get().Protocol,
        Envoy::Http::Headers::get().ProtocolValues.Bytestream},
       {Envoy::Http::Headers::get().Scheme, scheme},
       {Envoy::Http::Headers::get().Path, "/"},
       {Envoy::Http::Headers::get().Host, hostname_}});
  request_encoder_->encodeHeaders(*headers, false);
}

void HttpUpstream::resetEncoder(Network::ConnectionEvent event, bool inform_downstream) {
  if (!request_encoder_) {
    return;
  }
  request_encoder_->getStream().removeCallbacks(*this);
  if (!write_half_closed_ || !read_half_closed_) {
    request_encoder_->getStream().resetStream(Envoy::Http::StreamResetReason::LocalReset);
  }
  request_encoder_ = nullptr;
  if (inform_downstream) {
    upstream_callbacks_.onEvent(event);
  }
}

void HttpUpstream::doneReading() {
  read_half_closed_ = true;
  if (write_half_closed_) {
    resetEncoder(Network::ConnectionEvent::LocalClose);
  }
}

void HttpUpstream::doneWriting() {
  write_half_closed_ = true;
  if (read_half_closed_) {
    resetEncoder(Network::ConnectionEvent::LocalClose);
  }
}

void HttpConnectionHandle::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                         absl::string_view,
                                         Upstream::HostDescriptionConstSharedPtr host) {
  generic_pool_callbacks_.onPoolFailure(reason, host);
}

void HttpConnectionHandle::onPoolReady(Envoy::Http::RequestEncoder& request_encoder,
                                       Upstream::HostDescriptionConstSharedPtr host,
                                       StreamInfo::StreamInfo& info) {
  http_upstream_->setRequestEncoder(request_encoder,
                                    host->transportSocketFactory().implementsSecureTransport());
  // Disable cancel.
  upstream_http_handle_ = nullptr;
  generic_pool_callbacks_.onPoolReady(http_upstream_, host,
                                      request_encoder.getStream().connectionLocalAddress(), info);
}

} // namespace TcpProxy
} // namespace Envoy