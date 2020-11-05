#include "common/http/mixed_conn_pool.h"

#include "common/http/codec_client.h"
#include "common/http/http1/conn_pool.h"
#include "common/http/http2/conn_pool.h"
#include "common/http/utility.h"
#include "common/tcp/conn_pool.h"

namespace Envoy {
namespace Http {

Envoy::ConnectionPool::ActiveClientPtr HttpConnPoolImplMixed::instantiateActiveClient() {
  return std::make_unique<Tcp::ActiveTcpClient>(*this,
                                                Envoy::ConnectionPool::ConnPoolImplBase::host(), 1);
}

CodecClientPtr
HttpConnPoolImplMixed::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  auto protocol =
      protocol_ == Protocol::Http11 ? CodecClient::Type::HTTP1 : CodecClient::Type::HTTP2;
  CodecClientPtr codec{new CodecClientProd(protocol, std::move(data.connection_),
                                           data.host_description_, dispatcher_, random_generator_)};
  return codec;
}

void HttpConnPoolImplMixed::onConnected(Envoy::ConnectionPool::ActiveClient& client) {
  // When we upgrade from a TCP client to non-TCP we get a spurious onConnected
  // from the new client. Ignore that.
  if (client.protocol() != absl::nullopt) {
    return;
  }

  connected_ = true;
  // If an old TLS stack does not negotiate alpn, it likely does not support
  // HTTP/2. Fail over to HTTP/1.
  protocol_ = Protocol::Http11;
  auto tcp_client = static_cast<Tcp::ActiveTcpClient*>(&client);
  std::string alpn = tcp_client->connection_->nextProtocol();
  if (!alpn.empty()) {
    if (alpn == Http::Utility::AlpnNames::get().Http11) {
      protocol_ = Http::Protocol::Http11;
    } else if (alpn == Http::Utility::AlpnNames::get().Http2) {
      protocol_ = Http::Protocol::Http2;
    }
  }

  Upstream::Host::CreateConnectionData data{std::move(tcp_client->connection_),
                                            client.real_host_description_};
  data.connection_->removeConnectionCallbacks(*tcp_client);
  data.connection_->removeReadFilter(tcp_client->read_filter_handle_);
  dispatcher_.deferredDelete(client.removeFromList(owningList(client.state_)));

  std::unique_ptr<ActiveClient> new_client;
  if (protocol_ == Http::Protocol::Http11) {
    new_client = std::make_unique<Http1::ConnPoolImpl::ActiveClient>(*this, data);
  } else {
    new_client = std::make_unique<Http2::ConnPoolImpl::ActiveClient>(*this, data);
  }
  connecting_stream_capacity_ += new_client->effectiveConcurrentStreamLimit();
  new_client->state_ = ActiveClient::State::CONNECTING;
  LinkedList::moveIntoList(std::move(new_client), owningList(new_client->state_));
}

} // namespace Http
} // namespace Envoy
