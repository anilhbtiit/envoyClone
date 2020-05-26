#include "extensions/upstreams/http/tcp/upstream_request.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/status.h"
#include "envoy/http/conn_pool.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/scope_tracker.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/network/application_protocol.h"
#include "common/network/transport_socket_options_impl.h"
#include "common/network/upstream_server_name.h"
#include "common/network/upstream_subject_alt_names.h"
#include "common/router/config_impl.h"
#include "common/router/debug_config.h"
#include "common/router/router.h"
#include "common/stream_info/uint32_accessor_impl.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/common/proxy_protocol/proxy_protocol_header.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Router {

void TcpConnPool::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                              Upstream::HostDescriptionConstSharedPtr host) {
  upstream_handle_ = nullptr;
  Network::Connection& latched_conn = conn_data->connection();
  auto upstream =
      std::make_unique<TcpUpstream>(callbacks_->upstreamRequest(), std::move(conn_data));
  callbacks_->onPoolReady(std::move(upstream), host, latched_conn.localAddress(),
                          latched_conn.streamInfo());
}

TcpUpstream::TcpUpstream(UpstreamRequest* upstream_request,
                         Tcp::ConnectionPool::ConnectionDataPtr&& upstream)
    : upstream_request_(upstream_request), upstream_conn_data_(std::move(upstream)) {
  upstream_conn_data_->connection().enableHalfClose(true);
  upstream_conn_data_->addUpstreamCallbacks(*this);
}

void TcpUpstream::encodeData(Buffer::Instance& data, bool end_stream) {
  upstream_conn_data_->connection().write(data, end_stream);
}

void TcpUpstream::encodeHeaders(const Http::RequestHeaderMap&, bool end_stream) {
  // Headers should only happen once, so use this opportunity to add the proxy
  // proto header, if configured.
  ASSERT(upstream_request_->parent().routeEntry()->connectConfig().has_value());
  Buffer::OwnedImpl data;
  auto& connect_config = upstream_request_->parent().routeEntry()->connectConfig().value();
  if (connect_config.has_proxy_protocol_config()) {
    const Network::Connection& connection = *upstream_request_->parent().callbacks()->connection();
    Extensions::Common::ProxyProtocol::generateProxyProtoHeader(
        connect_config.proxy_protocol_config(), connection, data);
  }

  if (data.length() != 0 || end_stream) {
    upstream_conn_data_->connection().write(data, end_stream);
  }

  // TcpUpstream::encodeHeaders is called after the UpstreamRequest is fully initialized. Also use
  // this time to synthesize the 200 response headers downstream to complete the CONNECT handshake.
  Http::ResponseHeaderMapPtr headers{
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>({{Http::Headers::get().Status, "200"}})};
  upstream_request_->decodeHeaders(std::move(headers), false);
}

void TcpUpstream::encodeTrailers(const Http::RequestTrailerMap&) {
  Buffer::OwnedImpl data;
  upstream_conn_data_->connection().write(data, true);
}

void TcpUpstream::readDisable(bool disable) {
  if (upstream_conn_data_->connection().state() != Network::Connection::State::Open) {
    return;
  }
  upstream_conn_data_->connection().readDisable(disable);
}

void TcpUpstream::resetStream() {
  upstream_request_ = nullptr;
  upstream_conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
}

void TcpUpstream::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  upstream_request_->decodeData(data, end_stream);
}

void TcpUpstream::onEvent(Network::ConnectionEvent event) {
  if (event != Network::ConnectionEvent::Connected && upstream_request_) {
    upstream_request_->onResetStream(Http::StreamResetReason::ConnectionTermination, "");
  }
}

void TcpUpstream::onAboveWriteBufferHighWatermark() {
  if (upstream_request_) {
    upstream_request_->disableDataFromDownstreamForFlowControl();
  }
}

void TcpUpstream::onBelowWriteBufferLowWatermark() {
  if (upstream_request_) {
    upstream_request_->enableDataFromDownstreamForFlowControl();
  }
}

} // namespace Router
} // namespace Envoy
