#include "extensions/transport_sockets/proxy_protocol/proxy_protocol.h"

#include <sstream>

#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/network/transport_socket.h"

#include "common/buffer/buffer_impl.h"
#include "common/network/address_impl.h"

#include "extensions/common/proxy_protocol/proxy_protocol_header.h"

using envoy::config::core::v3::ProxyProtocolConfig_Version;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V1_AF_INET;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V1_AF_INET6;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V1_SIGNATURE;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_ADDR_LEN_INET;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_ADDR_LEN_INET6;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_AF_INET;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_AF_INET6;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_ONBEHALF_OF;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_SIGNATURE;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_SIGNATURE_LEN;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_TRANSPORT_STREAM;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_VERSION;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace ProxyProtocol {

ProxyProtocolSocket::ProxyProtocolSocket(Network::TransportSocketPtr transport_socket,
                                         Network::TransportSocketOptionsSharedPtr options,
                                         ProxyProtocolConfig_Version version)
    : transport_socket_(std::move(transport_socket)), options_(options), version_(version) {}

void ProxyProtocolSocket::setTransportSocketCallbacks(
    Network::TransportSocketCallbacks& callbacks) {
  transport_socket_->setTransportSocketCallbacks(callbacks);
  callbacks_ = &callbacks;
}

std::string ProxyProtocolSocket::protocol() const { return transport_socket_->protocol(); }

absl::string_view ProxyProtocolSocket::failureReason() const {
  return transport_socket_->failureReason();
}

bool ProxyProtocolSocket::canFlushClose() { return transport_socket_->canFlushClose(); }

void ProxyProtocolSocket::closeSocket(Network::ConnectionEvent event) {
  transport_socket_->closeSocket(event);
}

Network::IoResult ProxyProtocolSocket::doRead(Buffer::Instance& buffer) {
  return transport_socket_->doRead(buffer);
}

Network::IoResult ProxyProtocolSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  if (!generated_header_) {
    generateHeader();
    generated_header_ = true;
  }
  if (header_buffer_.length() > 0) {
    auto header_res = writeHeader();
    if (header_buffer_.length() == 0 && header_res.action_ == Network::PostIoAction::KeepOpen) {
      auto inner_res = transport_socket_->doWrite(buffer, end_stream);
      return {inner_res.action_, header_res.bytes_processed_ + inner_res.bytes_processed_, false};
    }
    return header_res;
  } else {
    return transport_socket_->doWrite(buffer, end_stream);
  }
}

void ProxyProtocolSocket::generateHeader() {
  if (version_ == ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V1) {
    generateHeaderV1();
  } else {
    generateHeaderV2();
  }
}

void ProxyProtocolSocket::generateHeaderV1() {
  // Default to local addresses
  auto src_addr = callbacks_->connection().localAddress();
  auto dst_addr = callbacks_->connection().remoteAddress();

  if (options_ && options_->proxyProtocolHeader().has_value()) {
    const auto header = options_->proxyProtocolHeader().value();
    src_addr = header.src_addr_;
    dst_addr = header.dst_addr_;
  }

  Common::ProxyProtocol::generateV1Header(*src_addr->ip(), *dst_addr->ip(), header_buffer_);
}

void ProxyProtocolSocket::generateHeaderV2() {
  if (!options_ || !options_->proxyProtocolHeader().has_value()) {
    Common::ProxyProtocol::generateV2LocalHeader(header_buffer_);
  } else {
    const auto header = options_->proxyProtocolHeader().value();
    Common::ProxyProtocol::generateV2Header(*header.src_addr_->ip(), *header.dst_addr_->ip(),
                                            header_buffer_);
  }
}

Network::IoResult ProxyProtocolSocket::writeHeader() {
  Network::PostIoAction action = Network::PostIoAction::KeepOpen;
  uint64_t bytes_written = 0;
  do {
    if (header_buffer_.length() == 0) {
      break;
    }

    Api::IoCallUint64Result result = header_buffer_.write(callbacks_->ioHandle());

    if (result.ok()) {
      ENVOY_CONN_LOG(trace, "write returns: {}", callbacks_->connection(), result.rc_);
      bytes_written += result.rc_;
    } else {
      ENVOY_CONN_LOG(trace, "write error: {}", callbacks_->connection(),
                     result.err_->getErrorDetails());
      if (result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
        action = Network::PostIoAction::Close;
      }
      break;
    }
  } while (true);

  return {action, bytes_written, false};
}

void ProxyProtocolSocket::onConnected() { transport_socket_->onConnected(); }

Ssl::ConnectionInfoConstSharedPtr ProxyProtocolSocket::ssl() const {
  return transport_socket_->ssl();
}

} // namespace ProxyProtocol
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy