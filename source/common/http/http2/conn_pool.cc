#include "common/http/http2/conn_pool.h"

#include <cstdint>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/upstream/upstream.h"

#include "common/http/http2/codec_impl.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Http {
namespace Http2 {

ConnPoolImpl::ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                           Upstream::ResourcePriority priority,
                           const Network::ConnectionSocket::OptionsSharedPtr& options,
                           const Network::TransportSocketOptionsSharedPtr& transport_socket_options)
    : ConnPoolImplBase(std::move(host), std::move(priority), dispatcher, options,
                       transport_socket_options) {}

ConnPoolImplBase::ActiveClientPtr ConnPoolImpl::instantiateActiveClient() {
  return std::make_unique<ActiveClient>(*this);
}
void ConnPoolImpl::onGoAway(ActiveClient& client) {
  ENVOY_CONN_LOG(debug, "remote goaway", *client.codec_client_);
  host_->cluster().stats().upstream_cx_close_notify_.inc();
  if (client.state_ != ActiveClient::State::DRAINING) {
    if (client.codec_client_->numActiveRequests() == 0) {
      client.codec_client_->close();
    } else {
      setActiveClientState(client, ActiveClient::State::DRAINING);
    }
  }
}

void ConnPoolImpl::onStreamDestroy(ActiveClient& client) {
  onRequestClosed(client, false);

  // If we are destroying this stream because of a disconnect, do not check for drain here. We will
  // wait until the connection has been fully drained of streams and then check in the connection
  // event callback.
  if (!client.closed_with_active_rq_) {
    checkForDrained();
  }
}

void ConnPoolImpl::onStreamReset(ActiveClient& client, Http::StreamResetReason reason) {
  if (reason == StreamResetReason::ConnectionTermination ||
      reason == StreamResetReason::ConnectionFailure) {
    host_->cluster().stats().upstream_rq_pending_failure_eject_.inc();
    client.closed_with_active_rq_ = true;
  } else if (reason == StreamResetReason::LocalReset) {
    host_->cluster().stats().upstream_rq_tx_reset_.inc();
  } else if (reason == StreamResetReason::RemoteReset) {
    host_->cluster().stats().upstream_rq_rx_reset_.inc();
  }
}

uint64_t ConnPoolImpl::maxRequestsPerConnection() {
  uint64_t max_streams = host_->cluster().maxRequestsPerConnection();
  if (max_streams == 0) {
    max_streams = maxTotalStreams();
  }
  return max_streams;
}

ConnPoolImpl::ActiveClient::ActiveClient(ConnPoolImpl& parent)
    : ConnPoolImplBase::ActiveClient(
          parent, parent.maxRequestsPerConnection(),
          std::numeric_limits<uint64_t>::max() /* TODO(ggreenway): get limit from config */) {
  codec_client_->setCodecClientCallbacks(*this);
  codec_client_->setCodecConnectionCallbacks(*this);

  parent.host_->cluster().stats().upstream_cx_http2_total_.inc();
}

bool ConnPoolImpl::ActiveClient::hasActiveRequests() const {
  return codec_client_->numActiveRequests() > 0;
}

bool ConnPoolImpl::ActiveClient::closingWithIncompleteRequest() const {
  return closed_with_active_rq_;
}

StreamEncoder& ConnPoolImpl::ActiveClient::newStreamEncoder(StreamDecoder& response_decoder) {
  return codec_client_->newStream(response_decoder);
}

CodecClientPtr ProdConnPoolImpl::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  CodecClientPtr codec{new CodecClientProd(CodecClient::Type::HTTP2, std::move(data.connection_),
                                           data.host_description_, dispatcher_)};
  return codec;
}

uint32_t ProdConnPoolImpl::maxTotalStreams() { return MAX_STREAMS; }

} // namespace Http2
} // namespace Http
} // namespace Envoy
