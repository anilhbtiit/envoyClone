#include "extensions/filters/listener/proxy_protocol/proxy_protocol.h"

#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/stats.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ProxyProtocol {

Config::Config(Stats::Scope& scope) : stats_{ALL_PROXY_PROTOCOL_STATS(POOL_COUNTER(scope))} {}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "proxy_protocol: New connection accepted");
  Network::ConnectionSocket& socket = cb.socket();
  ASSERT(file_event_.get() == nullptr);
  file_event_ =
      cb.dispatcher().createFileEvent(socket.fd(),
                                      [this](uint32_t events) {
                                        ASSERT(events == Event::FileReadyType::Read);
                                        onRead();
                                      },
                                      Event::FileTriggerType::Edge, Event::FileReadyType::Read);
  cb_ = &cb;
  return Network::FilterStatus::StopIteration;
}

void Filter::onRead() {
  try {
    onReadWorker();
  } catch (const EnvoyException& ee) {
    config_->stats_.downstream_cx_proxy_proto_error_.inc();
    cb_->continueFilterChain(false);
  }
}

void Filter::onReadWorker() {
  Network::ConnectionSocket& socket = cb_->socket();

  if ((!proxyProtocolHeader_.has_value() && !readProxyHeader(socket.fd())) ||
      (proxyProtocolHeader_.has_value() && proxyProtocolHeader_.value().extensions_length &&
       !parseExtensions(socket.fd()))) {
    return;
  }

  if (proxyProtocolHeader_.has_value()) {
    // Error check the source and destination fields. Most errors are caught by the address
    // parsing above, but a malformed IPv6 address may combine with a malformed port and parse as
    // an IPv6 address when parsing for an IPv4 address. Remote address refers to the source
    // address.
    const auto remote_version = proxyProtocolHeader_.value().remote_address->ip()->version();
    const auto local_version = proxyProtocolHeader_.value().local_address->ip()->version();
    if (remote_version != proxyProtocolHeader_.value().protocol_version ||
        local_version != proxyProtocolHeader_.value().protocol_version) {
      throw EnvoyException("failed to read proxy protocol");
    }
    // Check that both addresses are valid unicast addresses, as required for TCP
    if (!proxyProtocolHeader_.value().remote_address->ip()->isUnicastAddress() ||
        !proxyProtocolHeader_.value().local_address->ip()->isUnicastAddress()) {
      throw EnvoyException("failed to read proxy protocol");
    }

    // Only set the local address if it really changed, and mark it as address being restored.
    if (*proxyProtocolHeader_.value().local_address != *socket.localAddress()) {
      socket.setLocalAddress(proxyProtocolHeader_.value().local_address, true);
    }
    socket.setRemoteAddress(proxyProtocolHeader_.value().remote_address);
  }

  // Release the file event so that we do not interfere with the connection read events.
  file_event_.reset();
  cb_->continueFilterChain(true);
}

size_t Filter::lenV2Address(char* buf) {
  const uint8_t proto_family = buf[PROXY_PROTO_V2_SIGNATURE_LEN + 1];
  size_t len;

  switch ((proto_family & 0xf0) >> 4) {
  case PROXY_PROTO_V2_AF_INET:
    len = PROXY_PROTO_V2_ADDR_LEN_INET;
    break;
  case PROXY_PROTO_V2_AF_INET6:
    len = PROXY_PROTO_V2_ADDR_LEN_INET6;
    break;
  default:
    throw EnvoyException("Unsupported V2 proxy protocol address family");
  }
  return len;
}

void Filter::parseV2Header(char* buf) {
  // Skip the first 12-bytes
  // Next is ver/cmd
  const int ver_cmd = buf[PROXY_PROTO_V2_SIGNATURE_LEN];
  uint8_t u = buf[PROXY_PROTO_V2_HEADER_LEN - 2];
  uint8_t l = buf[PROXY_PROTO_V2_HEADER_LEN - 1];
  size_t hdr_addr_len = (u << 8) + l;

  // Only do connections on behalf of another user, not
  // internally-driven health-checks. If its not on behalf
  // of someone, or its not AF_INET{6} / STREAM/DGRAM, ignore
  // and use the real-remote info
  if ((ver_cmd & 0xf) == PROXY_PROTO_V2_ONBEHALF_OF) {
    uint8_t proto_family = buf[PROXY_PROTO_V2_SIGNATURE_LEN + 1];
    if ((((proto_family & 0xf0) >> 4) == PROXY_PROTO_V2_AF_INET ||
         ((proto_family & 0xf0) >> 4) == PROXY_PROTO_V2_AF_INET6) &&
        (((proto_family & 0x0f) == PROXY_PROTO_V2_TRANSPORT_STREAM) ||
         ((proto_family & 0x0f) == PROXY_PROTO_V2_TRANSPORT_DGRAM))) {
      if (((proto_family & 0xf0) >> 4) == PROXY_PROTO_V2_AF_INET) {
        typedef struct {
          uint32_t src_addr;
          uint32_t dst_addr;
          uint16_t src_port;
          uint16_t dst_port;
        } __attribute__((packed)) pp_ipv4_addr;
        pp_ipv4_addr* v4;
        v4 = reinterpret_cast<pp_ipv4_addr*>(&buf[PROXY_PROTO_V2_HEADER_LEN]);
        sockaddr_in ra4, la4;
        ra4.sin_family = AF_INET;
        ra4.sin_port = (v4->src_port);
        ra4.sin_addr.s_addr = (v4->src_addr);

        la4.sin_port = (v4->dst_port);
        la4.sin_addr.s_addr = (v4->dst_addr);
        proxyProtocolHeader_.emplace(
            WireHeader{hdr_addr_len - PROXY_PROTO_V2_ADDR_LEN_INET, Network::Address::IpVersion::v4,
                       std::make_shared<Network::Address::Ipv4Instance>(&ra4),
                       std::make_shared<Network::Address::Ipv4Instance>(&la4)});
      } else if (((proto_family & 0xf0) >> 4) == PROXY_PROTO_V2_AF_INET6) {
        typedef struct {
          uint8_t src_addr[16];
          uint8_t dst_addr[16];
          uint16_t src_port;
          uint16_t dst_port;
        } __attribute__((packed)) pp_ipv6_addr;
        pp_ipv6_addr* v6;
        v6 = reinterpret_cast<pp_ipv6_addr*>(&buf[PROXY_PROTO_V2_HEADER_LEN]);
        sockaddr_in6 ra6, la6;
        ra6.sin6_family = AF_INET;
        ra6.sin6_port = (v6->src_port);
        memcpy(ra6.sin6_addr.s6_addr, v6->src_addr, sizeof(ra6.sin6_addr.s6_addr));

        la6.sin6_port = (v6->dst_port);
        memcpy(la6.sin6_addr.s6_addr, v6->dst_addr, sizeof(la6.sin6_addr.s6_addr));

        proxyProtocolHeader_.emplace(WireHeader{
            hdr_addr_len - PROXY_PROTO_V2_ADDR_LEN_INET6, Network::Address::IpVersion::v6,
            std::make_shared<Network::Address::Ipv6Instance>(ra6),
            std::make_shared<Network::Address::Ipv6Instance>(la6)});
      }
    }
  } else if ((ver_cmd & 0xf) != PROXY_PROTO_V2_LOCAL) {
    // PROXY_PROTO_V2_LOCAL indicates its established as e.g. health-check, local
    // other values must be rejected
    throw EnvoyException("Unsupported V2 proxy protocol command");
  }
}

void Filter::parseV1Header(char* buf, size_t len) {
  std::string proxy_line;
  proxy_line.assign(buf, len);
  const auto trimmed_proxy_line = StringUtil::rtrim(proxy_line);

  // Parse proxy protocol line with format: PROXY TCP4/TCP6/UNKNOWN SOURCE_ADDRESS
  // DESTINATION_ADDRESS SOURCE_PORT DESTINATION_PORT.
  const auto line_parts = StringUtil::splitToken(trimmed_proxy_line, " ", true);
  if (line_parts.size() < 2 || line_parts[0] != "PROXY") {
    throw EnvoyException("failed to read proxy protocol");
  }

  // If the line starts with UNKNOWN we know it's a proxy protocol line, so we can remove it from
  // the socket and continue. According to spec "real connection's parameters" should be used, so
  // we should NOT restore the addresses in this case.
  if (line_parts[1] != "UNKNOWN") {
    // If protocol not UNKNOWN, src and dst addresses have to be present.
    if (line_parts.size() != 6) {
      throw EnvoyException("failed to read proxy protocol");
    }

    // TODO(gsagula): parseInternetAddressAndPort() could be modified to take two string_view
    // arguments, so we can eliminate allocation here.
    if (line_parts[1] == "TCP4") {
      proxyProtocolHeader_.emplace(
          WireHeader{0, Network::Address::IpVersion::v4,
                     Network::Utility::parseInternetAddressAndPort(
                         std::string{line_parts[2]} + ":" + std::string{line_parts[4]}),
                     Network::Utility::parseInternetAddressAndPort(
                         std::string{line_parts[3]} + ":" + std::string{line_parts[5]})});
    } else if (line_parts[1] == "TCP6") {
      proxyProtocolHeader_.emplace(
          WireHeader{0, Network::Address::IpVersion::v6,
                     Network::Utility::parseInternetAddressAndPort(
                         "[" + std::string{line_parts[2]} + "]:" + std::string{line_parts[4]}),
                     Network::Utility::parseInternetAddressAndPort(
                         "[" + std::string{line_parts[3]} + "]:" + std::string{line_parts[5]})});
    } else {
      throw EnvoyException("failed to read proxy protocol");
    }
  }
}

bool Filter::parseExtensions(int fd) {
  while (proxyProtocolHeader_.value().extensions_length) {
    // buf_ is no longer in use so we re-use it to read/discard
    int bytes_avail;
    auto& os_syscalls = Api::OsSysCallsSingleton::get();
    if (os_syscalls.ioctl(fd, FIONREAD, &bytes_avail) < 0) {
      throw EnvoyException("failed to read proxy protocol (no bytes avail)");
    }
    if (bytes_avail == 0) {
      return false;
    }
    bytes_avail = std::min(size_t(bytes_avail), sizeof(buf_));
    bytes_avail = std::min(size_t(bytes_avail), proxyProtocolHeader_.value().extensions_length);
    ssize_t nread = os_syscalls.recv(fd, buf_, bytes_avail, 0);
    if (nread == -1 && errno == EAGAIN) {
      return false;
    } else if (nread != bytes_avail) {
      throw EnvoyException("failed to read proxy protocol extension");
    }
    proxyProtocolHeader_.value().extensions_length -= nread;
  }
  return true;
}

bool Filter::readProxyHeader(int fd) {
  while (buf_off_ < MAX_PROXY_PROTO_LEN) {
    int bytes_avail;
    auto& os_syscalls = Api::OsSysCallsSingleton::get();

    if (os_syscalls.ioctl(fd, FIONREAD, &bytes_avail) < 0) {
      throw EnvoyException("failed to read proxy protocol (no bytes avail)");
    }

    if (bytes_avail == 0) {
      return false;
    }

    bytes_avail = std::min(size_t(bytes_avail), MAX_PROXY_PROTO_LEN - buf_off_);

    ssize_t nread = os_syscalls.recv(fd, buf_ + buf_off_, bytes_avail, MSG_PEEK);

    if (nread == -1 && errno == EAGAIN) {
      return false;
    } else if (nread < 1) {
      throw EnvoyException("failed to read proxy protocol (no bytes read)");
    }

    if (buf_off_ + nread >= PROXY_PROTO_V2_HEADER_LEN) {
      const char* sig = PROXY_PROTO_V2_SIGNATURE;
      if (!memcmp(buf_, sig, PROXY_PROTO_V2_SIGNATURE_LEN)) {
        header_version_ = V2;
      } else if (memcmp(buf_, PROXY_PROTO_V1_SIGNATURE, PROXY_PROTO_V1_SIGNATURE_LEN)) {
        // It is not v2, and can't be v1, so no sense hanging around: it is invalid
        throw EnvoyException("failed to read proxy protocol (exceed max v1 header len)");
      }
    }

    if (header_version_ == V2) {
      const int ver_cmd = buf_[PROXY_PROTO_V2_SIGNATURE_LEN];
      if (((ver_cmd & 0xf0) >> 4) != PROXY_PROTO_V2_VERSION) {
        throw EnvoyException("Unsupported V2 proxy protocol version");
      }
      if (buf_off_ < PROXY_PROTO_V2_HEADER_LEN) {
        ssize_t lread;
        ssize_t exp = PROXY_PROTO_V2_HEADER_LEN - buf_off_;
        lread = os_syscalls.recv(fd, buf_ + buf_off_, exp, 0);
        if (lread != exp) {
          throw EnvoyException("failed to read proxy protocol (insufficient data)");
        }
        buf_off_ += lread;
        nread -= lread;
      }
      ssize_t addrLen = lenV2Address(buf_);
      uint8_t u = buf_[PROXY_PROTO_V2_HEADER_LEN - 2];
      uint8_t l = buf_[PROXY_PROTO_V2_HEADER_LEN - 1];
      ssize_t hdr_addr_len = (u << 8) + l;
      if (hdr_addr_len < addrLen) {
        throw EnvoyException("failed to read proxy protocol (insufficient data)");
      }
      if (ssize_t(buf_off_) + nread >= PROXY_PROTO_V2_HEADER_LEN + addrLen) {
        int lread;
        int missing = (PROXY_PROTO_V2_HEADER_LEN + addrLen) - buf_off_;
        lread = os_syscalls.recv(fd, buf_ + buf_off_, missing, 0);
        if (lread != missing) {
          throw EnvoyException("failed to read proxy protocol (insufficient data)");
        }
        buf_off_ += lread;
        parseV2Header(buf_);
        // The TLV remain, they are read/discard in parseExtensions() which is called from the
        // parent (if needed).
        return true;
      } else {
        nread = os_syscalls.recv(fd, buf_ + buf_off_, nread, 0);
        if (nread < 0) {
          throw EnvoyException("failed to read proxy protocol (insufficient data)");
        }
        buf_off_ += nread;
      }
    } else {
      // continue searching buf_ from where we left off
      for (; search_index_ < buf_off_ + nread; search_index_++) {
        if (buf_[search_index_] == '\n' && buf_[search_index_ - 1] == '\r') {
          if (search_index_ == 1) {
            // This could be the binary protocol. It cannot be the ascii protocol
            header_version_ = InProgress;
          } else {
            header_version_ = V1;
            search_index_++;
          }
          break;
        }
      }

      // If we bailed on the first char, we might be v2, but are for sure not v1. Thus we
      // can read up to min(PROXY_PROTO_V2_HEADER_LEN, bytes_avail). If we bailed after first
      // char, but before we hit \r\n, read up to search_index_. We're asking only for
      // bytes we've already seen so there should be no block or fail
      size_t ntoread;
      if (header_version_ == InProgress) {
        ntoread = bytes_avail;
      } else {
        ntoread = search_index_ - buf_off_;
      }

      nread = os_syscalls.recv(fd, buf_ + buf_off_, ntoread, 0);
      ASSERT(size_t(nread) == ntoread);

      buf_off_ += nread;

      if (header_version_ == V1) {
        parseV1Header(buf_, buf_off_);
        return true;
      }
    }
  }

  throw EnvoyException("failed to read proxy protocol (exceed max v2 header len)");
}

} // namespace ProxyProtocol
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
