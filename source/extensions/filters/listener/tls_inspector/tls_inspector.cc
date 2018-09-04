#include "extensions/filters/listener/tls_inspector/tls_inspector.h"

#include <arpa/inet.h>

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"

#include "extensions/transport_sockets/well_known_names.h"

#include "openssl/bytestring.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {

Config::Config(Stats::Scope& scope, uint32_t max_client_hello_size, const std::string& stat_prefix)
    : stats_{TLS_STATS(POOL_COUNTER_PREFIX(scope, stat_prefix))},
      ssl_ctx_(SSL_CTX_new(TLS_with_buffers_method())),
      max_client_hello_size_(max_client_hello_size) {

  if (max_client_hello_size_ > TLS_MAX_CLIENT_HELLO) {
    throw EnvoyException(fmt::format("max_client_hello_size of {} is greater than maximum of {}.",
                                     max_client_hello_size_, size_t(TLS_MAX_CLIENT_HELLO)));
  }

  SSL_CTX_set_options(ssl_ctx_.get(), SSL_OP_NO_TICKET);
  SSL_CTX_set_session_cache_mode(ssl_ctx_.get(), SSL_SESS_CACHE_OFF);
  SSL_CTX_set_select_certificate_cb(
      ssl_ctx_.get(), [](const SSL_CLIENT_HELLO* client_hello) -> ssl_select_cert_result_t {
        const uint8_t* data;
        size_t len;
        if (SSL_early_callback_ctx_extension_get(
                client_hello, TLSEXT_TYPE_application_layer_protocol_negotiation, &data, &len)) {
          TlsFilterBase* filter = static_cast<TlsFilterBase*>(SSL_get_app_data(client_hello->ssl));
          filter->onALPN(data, len);
        }
        return ssl_select_cert_success;
      });
  SSL_CTX_set_tlsext_servername_callback(
      ssl_ctx_.get(), [](SSL* ssl, int* out_alert, void*) -> int {
        TlsFilterBase* filter = static_cast<TlsFilterBase*>(SSL_get_app_data(ssl));
        filter->onServername(SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name));

        // Return an error to stop the handshake; we have what we wanted already.
        *out_alert = SSL_AD_USER_CANCELLED;
        return SSL_TLSEXT_ERR_ALERT_FATAL;
      });
}

bssl::UniquePtr<SSL> Config::newSsl() { return bssl::UniquePtr<SSL>{SSL_new(ssl_ctx_.get())}; }

thread_local uint8_t Filter::buf_[Config::TLS_MAX_CLIENT_HELLO];

Filter::Filter(const ConfigSharedPtr config) : config_(config), ssl_(config_->newSsl()) {
  initializeSsl(config->maxClientHelloSize(), sizeof(buf_), ssl_,
                static_cast<TlsFilterBase*>(this));
}

void Filter::initializeSsl(uint32_t maxClientHelloSize, size_t bufSize,
                           const bssl::UniquePtr<SSL>& ssl, void* appData) {
  RELEASE_ASSERT(bufSize >= maxClientHelloSize, "");

  SSL_set_app_data(ssl.get(), appData);
  SSL_set_accept_state(ssl.get());
}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "tls inspector: new connection accepted");
  Network::ConnectionSocket& socket = cb.socket();
  ASSERT(file_event_ == nullptr);

  file_event_ = cb.dispatcher().createFileEvent(
      socket.fd(),
      [this](uint32_t events) {
        if (events & Event::FileReadyType::Closed) {
          config_->stats().connection_closed_.inc();
          done(false);
          return;
        }

        ASSERT(events == Event::FileReadyType::Read);
        onRead();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Closed);

  // TODO(PiotrSikora): make this configurable.
  timer_ = cb.dispatcher().createTimer([this]() -> void { onTimeout(); });
  timer_->enableTimer(std::chrono::milliseconds(15000));

  // TODO(ggreenway): Move timeout and close-detection to the filter manager
  // so that it applies to all listener filters.

  cb_ = &cb;
  return Network::FilterStatus::StopIteration;
}

void Filter::onALPN(const unsigned char* data, unsigned int len) {
  doOnALPN(data, len,
           [&](std::vector<absl::string_view> protocols) {
             cb_->socket().setRequestedApplicationProtocols(protocols);
           },
           alpn_found_);
}

void Filter::doOnALPN(const unsigned char* data, unsigned int len,
                      std::function<void(std::vector<absl::string_view> protocols)> onAlpnCb,
                      bool& alpn_found) {
  CBS wire, list;
  CBS_init(&wire, reinterpret_cast<const uint8_t*>(data), static_cast<size_t>(len));
  if (!CBS_get_u16_length_prefixed(&wire, &list) || CBS_len(&wire) != 0 || CBS_len(&list) < 2) {
    // Don't produce errors, let the real TLS stack do it.
    return;
  }
  CBS name;
  std::vector<absl::string_view> protocols;
  while (CBS_len(&list) > 0) {
    if (!CBS_get_u8_length_prefixed(&list, &name) || CBS_len(&name) == 0) {
      // Don't produce errors, let the real TLS stack do it.
      return;
    }
    protocols.emplace_back(reinterpret_cast<const char*>(CBS_data(&name)), CBS_len(&name));
  }
  onAlpnCb(protocols);
  alpn_found = true;
}

void Filter::onServername(absl::string_view servername) {
  ENVOY_LOG(debug, "tls:onServerName(), requestedServerName: {}", servername);
  doOnServername(
      servername, config_->stats(),
      [&](absl::string_view name) -> void { cb_->socket().setRequestedServerName(name); },
      clienthello_success_);
}

void Filter::doOnServername(absl::string_view name, const TlsStats& stats,
                            std::function<void(absl::string_view name)> onServernameCb,
                            bool& clienthello_success) {
  if (!name.empty()) {
    stats.sni_found_.inc();
    onServernameCb(name);
  } else {
    stats.sni_not_found_.inc();
  }
  clienthello_success = true;
}

void Filter::onRead() {
  // This receive code is somewhat complicated, because it must be done as a MSG_PEEK because
  // there is no way for a listener-filter to pass payload data to the ConnectionImpl and filters
  // that get created later.
  //
  // The file_event_ in this class gets events everytime new data is available on the socket,
  // even if previous data has not been read, which is always the case due to MSG_PEEK. When
  // the TlsInspector completes and passes the socket along, a new FileEvent is created for the
  // socket, so that new event is immediately signalled as readable because it is new and the socket
  // is readable, even though no new events have ocurred.
  //
  // TODO(ggreenway): write an integration test to ensure the events work as expected on all
  // platforms.
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const Api::SysCallSizeResult result =
      os_syscalls.recv(cb_->socket().fd(), buf_, config_->maxClientHelloSize(), MSG_PEEK);
  ENVOY_LOG(trace, "tls inspector: recv: {}", result.rc_);

  if (result.rc_ == -1 && result.errno_ == EAGAIN) {
    return;
  } else if (result.rc_ < 0) {
    config_->stats().read_error_.inc();
    done(false);
    return;
  }

  // Because we're doing a MSG_PEEK, data we've seen before gets returned every time, so
  // skip over what we've already processed.
  if (static_cast<uint64_t>(result.rc_) > read_) {
    const uint8_t* data = buf_ + read_;
    const size_t len = result.rc_ - read_;
    read_ = result.rc_;
    parseClientHello(data, len, ssl_, read_, config_->maxClientHelloSize(), config_->stats(),
                     [&](bool success) -> void { done(success); }, alpn_found_,
                     clienthello_success_,
                     [&]() -> void {
                       cb_->socket().setDetectedTransportProtocol(
                           TransportSockets::TransportSocketNames::get().Tls);
                     });
  }
}

void Filter::onTimeout() {
  ENVOY_LOG(trace, "tls inspector: timeout");
  config_->stats().read_timeout_.inc();
  done(false);
}

void Filter::done(bool success) {
  ENVOY_LOG(trace, "tls inspector: done: {}", success);
  timer_.reset();
  file_event_.reset();
  cb_->continueFilterChain(success);
}

void Filter::parseClientHello(const void* data, size_t len, bssl::UniquePtr<SSL>& ssl,
                              uint64_t read, uint32_t maxClientHelloSize, const TlsStats& stats,
                              std::function<void(bool)> done, bool& alpn_found,
                              bool& clienthello_success, std::function<void()> onSuccess) {
  // Ownership is passed to ssl in SSL_set_bio()
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(data, len));

  // Make the mem-BIO return that there is more data
  // available beyond it's end
  BIO_set_mem_eof_return(bio.get(), -1);

  SSL_set_bio(ssl.get(), bio.get(), bio.get());
  bio.release();

  int ret = SSL_do_handshake(ssl.get());

  // This should never succeed because an error is always returned from the SNI callback.
  ASSERT(ret <= 0);
  switch (SSL_get_error(ssl.get(), ret)) {
  case SSL_ERROR_WANT_READ:
    if (read == maxClientHelloSize) {
      // We've hit the specified size limit. This is an unreasonably large ClientHello;
      // indicate failure.
      stats.client_hello_too_large_.inc();
      done(false);
    }
    break;
  case SSL_ERROR_SSL:
    if (clienthello_success) {
      stats.tls_found_.inc();
      if (alpn_found) {
        stats.alpn_found_.inc();
      } else {
        stats.alpn_not_found_.inc();
      }
      onSuccess();
    } else {
      stats.tls_not_found_.inc();
    }
    done(true);
    break;
  default:
    done(false);
    break;
  }
}

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
