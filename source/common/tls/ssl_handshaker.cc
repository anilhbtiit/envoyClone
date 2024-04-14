#include "source/common/tls/ssl_handshaker.h"

#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/http/headers.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/tls/context_impl.h"
#include "source/common/tls/utility.h"

using Envoy::Network::PostIoAction;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

void ValidateResultCallbackImpl::onSslHandshakeCancelled() { extended_socket_info_.reset(); }

void ValidateResultCallbackImpl::onCertValidationResult(bool succeeded,
                                                        Ssl::ClientValidationStatus detailed_status,
                                                        const std::string& /*error_details*/,
                                                        uint8_t tls_alert) {
  if (!extended_socket_info_.has_value()) {
    return;
  }
  extended_socket_info_->setCertificateValidationStatus(detailed_status);
  extended_socket_info_->setCertificateValidationAlert(tls_alert);
  extended_socket_info_->onCertificateValidationCompleted(succeeded, true);
}

void CertSelectionCallbackImpl::onSslHandshakeCancelled() { extended_socket_info_.reset(); }

void CertSelectionCallbackImpl::onCertSelectionResult(bool succeeded,
                                                      const Ssl::TlsContext& selected_ctx,
                                                      bool staple) {
  ENVOY_LOG(debug, "onCertSelectionResult: {}, {}", succeeded, staple);
  if (!extended_socket_info_.has_value()) {
    ENVOY_LOG(debug, "extended socket info is gone, maybe connection terminated");
    return;
  }
  if (succeeded) {
    // Apply the selected context. This must be done before OCSP stapling below
    // since applying the context can remove the previously-set OCSP response.
    // This will only return NULL if memory allocation fails.
    RELEASE_ASSERT(SSL_set_SSL_CTX(ssl_, selected_ctx.ssl_ctx_.get()) != nullptr, "");

    if (staple) {
      // We avoid setting the OCSP response if the client didn't request it, but doing so is safe.
      RELEASE_ASSERT(selected_ctx.ocsp_response_,
                     "OCSP response must be present under OcspStapleAction::Staple");
      auto& resp_bytes = selected_ctx.ocsp_response_->rawBytes();
      const int rc = SSL_set_ocsp_response(ssl_, resp_bytes.data(), resp_bytes.size());
      RELEASE_ASSERT(rc != 0, "");
    }
  }
  extended_socket_info_->onCertSelectionCompleted(succeeded);
}

SslExtendedSocketInfoImpl::~SslExtendedSocketInfoImpl() {
  if (cert_validate_result_callback_.has_value()) {
    cert_validate_result_callback_->onSslHandshakeCancelled();
  }
  if (cert_selection_callback_.has_value()) {
    cert_selection_callback_->onSslHandshakeCancelled();
  }
}

void SslExtendedSocketInfoImpl::setCertificateValidationStatus(
    Envoy::Ssl::ClientValidationStatus validated) {
  certificate_validation_status_ = validated;
}

Envoy::Ssl::ClientValidationStatus SslExtendedSocketInfoImpl::certificateValidationStatus() const {
  return certificate_validation_status_;
}

void SslExtendedSocketInfoImpl::onCertificateValidationCompleted(bool succeeded, bool async) {
  cert_validation_result_ =
      succeeded ? Ssl::ValidateStatus::Successful : Ssl::ValidateStatus::Failed;
  if (cert_validate_result_callback_.has_value()) {
    cert_validate_result_callback_.reset();
    // Resume handshake.
    if (async) {
      ssl_handshaker_.handshakeCallbacks()->onAsynchronousCertValidationComplete();
    }
  }
}

Ssl::ValidateResultCallbackPtr SslExtendedSocketInfoImpl::createValidateResultCallback() {
  auto callback = std::make_unique<ValidateResultCallbackImpl>(
      ssl_handshaker_.handshakeCallbacks()->connection().dispatcher(), *this);
  cert_validate_result_callback_ = *callback;
  cert_validation_result_ = Ssl::ValidateStatus::Pending;
  return callback;
}

void SslExtendedSocketInfoImpl::onCertSelectionCompleted(bool succeeded) {
  RELEASE_ASSERT(cert_selection_result_ != Ssl::CertSelectionStatus::Successful &&
                     cert_selection_result_ != Ssl::CertSelectionStatus::Failed,
                 "onCertSelectionCompleted twice");
  const bool async = cert_selection_result_ == Ssl::CertSelectionStatus::Pending;
  cert_selection_result_ =
      succeeded ? Ssl::CertSelectionStatus::Successful : Ssl::CertSelectionStatus::Failed;
  if (cert_selection_callback_.has_value()) {
    cert_selection_callback_.reset();
    // Resume handshake.
    if (async) {
      ssl_handshaker_.handshakeCallbacks()->onAsynchronousCertSelectionComplete();
    }
  }
}

void SslExtendedSocketInfoImpl::setCertSelectionAsync() {
  RELEASE_ASSERT(cert_selection_result_ == Ssl::CertSelectionStatus::NotStarted,
                 "unexpected cert selection result");
  cert_selection_result_ = Ssl::CertSelectionStatus::Pending;
}

Ssl::CertSelectionCallbackPtr SslExtendedSocketInfoImpl::createCertSelectionCallback(SSL* ssl) {
  auto callback = std::make_unique<CertSelectionCallbackImpl>(
      ssl, ssl_handshaker_.handshakeCallbacks()->connection().dispatcher(), *this);
  cert_selection_callback_ = *callback;
  return callback;
}

SslHandshakerImpl::SslHandshakerImpl(bssl::UniquePtr<SSL> ssl, int ssl_extended_socket_info_index,
                                     Ssl::HandshakeCallbacks* handshake_callbacks)
    : ssl_(std::move(ssl)), handshake_callbacks_(handshake_callbacks),
      extended_socket_info_(*this) {
  SSL_set_ex_data(ssl_.get(), ssl_extended_socket_info_index, &(this->extended_socket_info_));
}

bool SslHandshakerImpl::peerCertificateValidated() const {
  return extended_socket_info_.certificateValidationStatus() ==
         Envoy::Ssl::ClientValidationStatus::Validated;
}

Network::PostIoAction SslHandshakerImpl::doHandshake() {
  ASSERT(state_ != Ssl::SocketState::HandshakeComplete && state_ != Ssl::SocketState::ShutdownSent);
  int rc = SSL_do_handshake(ssl());
  if (rc == 1) {
    state_ = Ssl::SocketState::HandshakeComplete;
    handshake_callbacks_->onSuccess(ssl());

    // It's possible that we closed during the handshake callback.
    return handshake_callbacks_->connection().state() == Network::Connection::State::Open
               ? PostIoAction::KeepOpen
               : PostIoAction::Close;
  } else {
    int err = SSL_get_error(ssl(), rc);
    ENVOY_CONN_LOG(trace, "ssl error occurred while read: {}", handshake_callbacks_->connection(),
                   Utility::getErrorDescription(err));
    switch (err) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      return PostIoAction::KeepOpen;
    case SSL_ERROR_PENDING_CERTIFICATE:
    case SSL_ERROR_WANT_PRIVATE_KEY_OPERATION:
    case SSL_ERROR_WANT_CERTIFICATE_VERIFY:
      state_ = Ssl::SocketState::HandshakeInProgress;
      return PostIoAction::KeepOpen;
    default:
      handshake_callbacks_->onFailure();
      return PostIoAction::Close;
    }
  }
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
