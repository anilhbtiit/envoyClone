#include "common/ssl/context_config_impl.h"

#include <memory>
#include <string>

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/config/datasource.h"
#include "common/config/tls_context_json.h"
#include "common/protobuf/utility.h"
#include "common/secret/sds_api.h"
#include "common/ssl/certificate_validation_context_config_impl.h"
#include "common/ssl/tls_certificate_config_impl.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {

namespace {

Secret::TlsCertificateConfigProviderSharedPtr getTlsCertificateConfigProvider(
    const envoy::api::v2::auth::CommonTlsContext& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Ssl::TlsCertificateConfigPtr* tls_config) {
  if (!config.tls_certificates().empty()) {
    const auto& tls_certificate = config.tls_certificates(0);
    if (!tls_certificate.has_certificate_chain() && !tls_certificate.has_private_key()) {
      return nullptr;
    }
    auto secret_provider = factory_context.secretManager().createInlineTlsCertificateProvider(
        config.tls_certificates(0));
    *tls_config = std::make_unique<Ssl::TlsCertificateConfigImpl>(*secret_provider->secret());
    return secret_provider;
  }
  if (!config.tls_certificate_sds_secret_configs().empty()) {
    const auto& sds_secret_config = config.tls_certificate_sds_secret_configs(0);
    if (!sds_secret_config.has_sds_config()) {
      // static secret
      auto secret_provider = factory_context.secretManager().findStaticTlsCertificateProvider(
          sds_secret_config.name());
      if (!secret_provider) {
        throw EnvoyException(fmt::format("Unknown static secret: {}", sds_secret_config.name()));
      }
      *tls_config = std::make_unique<Ssl::TlsCertificateConfigImpl>(*secret_provider->secret());
      return secret_provider;
    } else {
      return factory_context.secretManager().findOrCreateTlsCertificateProvider(
          sds_secret_config.sds_config(), sds_secret_config.name(), factory_context);
    }
  }
  return nullptr;
}

Secret::CertificateValidationContextConfigProviderSharedPtr
getProviderFromSds(Server::Configuration::TransportSocketFactoryContext& factory_context,
                   const envoy::api::v2::auth::SdsSecretConfig& sds_secret_config,
                   Ssl::CertificateValidationContextConfigPtr* cvc_config) {
  if (!sds_secret_config.has_sds_config()) {
    // static secret
    auto secret_provider =
        factory_context.secretManager().findStaticCertificateValidationContextProvider(
            sds_secret_config.name());
    if (!secret_provider) {
      throw EnvoyException(fmt::format("Unknown static certificate validation context: {}",
                                       sds_secret_config.name()));
    }
    *cvc_config =
        std::make_unique<Ssl::CertificateValidationContextConfigImpl>(*secret_provider->secret());
    return secret_provider;
  } else {
    auto secret_provider =
        factory_context.secretManager().findOrCreateCertificateValidationContextProvider(
            sds_secret_config.sds_config(), sds_secret_config.name(), factory_context);
    return secret_provider;
  }
  return nullptr;
}

Secret::CertificateValidationContextConfigProviderSharedPtr
getCertificateValidationContextConfigProvider(
    const envoy::api::v2::auth::CommonTlsContext& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Ssl::CertificateValidationContextConfigPtr* cvc_config,
    std::unique_ptr<envoy::api::v2::auth::CertificateValidationContext>* default_cvc) {
  switch (config.validation_context_type_case()) {
  case envoy::api::v2::auth::CommonTlsContext::ValidationContextTypeCase::kValidationContext: {
    auto secret_provider =
        factory_context.secretManager().createInlineCertificateValidationContextProvider(
            config.validation_context());
    *cvc_config =
        std::make_unique<Ssl::CertificateValidationContextConfigImpl>(*secret_provider->secret());
    return secret_provider;
  }
  case envoy::api::v2::auth::CommonTlsContext::ValidationContextTypeCase::
      kValidationContextSdsSecretConfig: {
    const auto& sds_secret_config = config.validation_context_sds_secret_config();
    return getProviderFromSds(factory_context, sds_secret_config, cvc_config);
  }
  case envoy::api::v2::auth::CommonTlsContext::ValidationContextTypeCase::
      kCombinedValidationContext: {
    *default_cvc = std::make_unique<envoy::api::v2::auth::CertificateValidationContext>(
        config.combined_validation_context().default_validation_context());
    const auto& sds_secret_config =
        config.combined_validation_context().validation_context_sds_secret_config();
    return getProviderFromSds(factory_context, sds_secret_config, cvc_config);
  }
  default:
    return nullptr;
  }
}

} // namespace

const std::string ContextConfigImpl::DEFAULT_CIPHER_SUITES =
    "[ECDHE-ECDSA-AES128-GCM-SHA256|ECDHE-ECDSA-CHACHA20-POLY1305]:"
    "[ECDHE-RSA-AES128-GCM-SHA256|ECDHE-RSA-CHACHA20-POLY1305]:"
    "ECDHE-ECDSA-AES128-SHA:"
    "ECDHE-RSA-AES128-SHA:"
    "AES128-GCM-SHA256:"
    "AES128-SHA:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-AES256-SHA:"
    "ECDHE-RSA-AES256-SHA:"
    "AES256-GCM-SHA384:"
    "AES256-SHA";

const std::string ContextConfigImpl::DEFAULT_ECDH_CURVES = "X25519:P-256";

ContextConfigImpl::ContextConfigImpl(
    const envoy::api::v2::auth::CommonTlsContext& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context)
    : alpn_protocols_(RepeatedPtrUtil::join(config.alpn_protocols(), ",")),
      cipher_suites_(StringUtil::nonEmptyStringOrDefault(
          RepeatedPtrUtil::join(config.tls_params().cipher_suites(), ":"), DEFAULT_CIPHER_SUITES)),
      ecdh_curves_(StringUtil::nonEmptyStringOrDefault(
          RepeatedPtrUtil::join(config.tls_params().ecdh_curves(), ":"), DEFAULT_ECDH_CURVES)),
      tls_certficate_provider_(
          getTlsCertificateConfigProvider(config, factory_context, &tls_certificate_config_)),
      certficate_validation_context_provider_(getCertificateValidationContextConfigProvider(
          config, factory_context, &validation_context_config_, &default_cvc_)),
      min_protocol_version_(
          tlsVersionFromProto(config.tls_params().tls_minimum_protocol_version(), TLS1_VERSION)),
      max_protocol_version_(
          tlsVersionFromProto(config.tls_params().tls_maximum_protocol_version(), TLS1_2_VERSION)) {
  if (default_cvc_) {
    cvc_validation_callback_handle_ =
        dynamic_cast<Secret::CertificateValidationContextSdsApi*>(
            certficate_validation_context_provider_.get())
            ->addValidationCallback(
                [this](const envoy::api::v2::auth::CertificateValidationContext& dynamic_cvc) {
                  getCombinedValidationContextConfig(dynamic_cvc);
                  validation_context_config_.reset();
                });
  }
}

Ssl::CertificateValidationContextConfigPtr ContextConfigImpl::getCombinedValidationContextConfig(
    const envoy::api::v2::auth::CertificateValidationContext& dynamic_cvc) {
  envoy::api::v2::auth::CertificateValidationContext combined_cvc = *default_cvc_.get();
  combined_cvc.MergeFrom(dynamic_cvc);
  return std::make_unique<CertificateValidationContextConfigImpl>(combined_cvc);
}

void ContextConfigImpl::setSecretUpdateCallback(std::function<void()> callback) {
  if (tls_certficate_provider_) {
    if (tc_update_callback_handle_) {
      tc_update_callback_handle_->remove();
    }
    tc_update_callback_handle_ = tls_certficate_provider_->addUpdateCallback([this, callback]() {
      tls_certificate_config_ =
          std::make_unique<Ssl::TlsCertificateConfigImpl>(*tls_certficate_provider_->secret());
      callback();
    });
  }
  if (certficate_validation_context_provider_) {
    if (cvc_update_callback_handle_) {
      cvc_update_callback_handle_->remove();
    }
    if (default_cvc_) {
      cvc_update_callback_handle_ =
          certficate_validation_context_provider_->addUpdateCallback([this, callback]() {
            validation_context_config_ = getCombinedValidationContextConfig(
                *certficate_validation_context_provider_->secret());
            callback();
          });
    } else {
      cvc_update_callback_handle_ =
          certficate_validation_context_provider_->addUpdateCallback([this, callback]() {
            validation_context_config_ =
                std::make_unique<Ssl::CertificateValidationContextConfigImpl>(
                    *certficate_validation_context_provider_->secret());
            callback();
          });
    }
  }
}

ContextConfigImpl::~ContextConfigImpl() {
  if (tc_update_callback_handle_) {
    tc_update_callback_handle_->remove();
  }
  if (cvc_update_callback_handle_) {
    cvc_update_callback_handle_->remove();
  }
  if (cvc_validation_callback_handle_) {
    cvc_validation_callback_handle_->remove();
  }
}

unsigned ContextConfigImpl::tlsVersionFromProto(
    const envoy::api::v2::auth::TlsParameters_TlsProtocol& version, unsigned default_version) {
  switch (version) {
  case envoy::api::v2::auth::TlsParameters::TLS_AUTO:
    return default_version;
  case envoy::api::v2::auth::TlsParameters::TLSv1_0:
    return TLS1_VERSION;
  case envoy::api::v2::auth::TlsParameters::TLSv1_1:
    return TLS1_1_VERSION;
  case envoy::api::v2::auth::TlsParameters::TLSv1_2:
    return TLS1_2_VERSION;
  case envoy::api::v2::auth::TlsParameters::TLSv1_3:
    return TLS1_3_VERSION;
  default:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

ClientContextConfigImpl::ClientContextConfigImpl(
    const envoy::api::v2::auth::UpstreamTlsContext& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context)
    : ContextConfigImpl(config.common_tls_context(), factory_context),
      server_name_indication_(config.sni()), allow_renegotiation_(config.allow_renegotiation()) {
  // BoringSSL treats this as a C string, so embedded NULL characters will not
  // be handled correctly.
  if (server_name_indication_.find('\0') != std::string::npos) {
    throw EnvoyException("SNI names containing NULL-byte are not allowed");
  }
  // TODO(PiotrSikora): Support multiple TLS certificates.
  if ((config.common_tls_context().tls_certificates().size() +
       config.common_tls_context().tls_certificate_sds_secret_configs().size()) > 1) {
    throw EnvoyException("Multiple TLS certificates are not supported for client contexts");
  }
}

ClientContextConfigImpl::ClientContextConfigImpl(
    const Json::Object& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context)
    : ClientContextConfigImpl(
          [&config] {
            envoy::api::v2::auth::UpstreamTlsContext upstream_tls_context;
            Config::TlsContextJson::translateUpstreamTlsContext(config, upstream_tls_context);
            return upstream_tls_context;
          }(),
          factory_context) {}

ServerContextConfigImpl::ServerContextConfigImpl(
    const envoy::api::v2::auth::DownstreamTlsContext& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context)
    : ContextConfigImpl(config.common_tls_context(), factory_context),
      require_client_certificate_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, require_client_certificate, false)),
      session_ticket_keys_([&config] {
        std::vector<SessionTicketKey> ret;

        switch (config.session_ticket_keys_type_case()) {
        case envoy::api::v2::auth::DownstreamTlsContext::kSessionTicketKeys:
          for (const auto& datasource : config.session_ticket_keys().keys()) {
            validateAndAppendKey(ret, Config::DataSource::read(datasource, false));
          }
          break;
        case envoy::api::v2::auth::DownstreamTlsContext::kSessionTicketKeysSdsSecretConfig:
          throw EnvoyException("SDS not supported yet");
          break;
        case envoy::api::v2::auth::DownstreamTlsContext::SESSION_TICKET_KEYS_TYPE_NOT_SET:
          break;
        default:
          throw EnvoyException(fmt::format("Unexpected case for oneof session_ticket_keys: {}",
                                           config.session_ticket_keys_type_case()));
        }

        return ret;
      }()) {
  // TODO(PiotrSikora): Support multiple TLS certificates.
  if ((config.common_tls_context().tls_certificates().size() +
       config.common_tls_context().tls_certificate_sds_secret_configs().size()) == 0) {
    throw EnvoyException("No TLS certificates found for server context");
  } else if ((config.common_tls_context().tls_certificates().size() +
              config.common_tls_context().tls_certificate_sds_secret_configs().size()) > 1) {
    throw EnvoyException("A single TLS certificate is required for server contexts");
  }
}

ServerContextConfigImpl::ServerContextConfigImpl(
    const Json::Object& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context)
    : ServerContextConfigImpl(
          [&config] {
            envoy::api::v2::auth::DownstreamTlsContext downstream_tls_context;
            Config::TlsContextJson::translateDownstreamTlsContext(config, downstream_tls_context);
            return downstream_tls_context;
          }(),
          factory_context) {}

// Append a SessionTicketKey to keys, initializing it with key_data.
// Throws if key_data is invalid.
void ServerContextConfigImpl::validateAndAppendKey(
    std::vector<ServerContextConfig::SessionTicketKey>& keys, const std::string& key_data) {
  // If this changes, need to figure out how to deal with key files
  // that previously worked. For now, just assert so we'll notice that
  // it changed if it does.
  static_assert(sizeof(SessionTicketKey) == 80, "Input is expected to be this size");

  if (key_data.size() != sizeof(SessionTicketKey)) {
    throw EnvoyException(fmt::format("Incorrect TLS session ticket key length. "
                                     "Length {}, expected length {}.",
                                     key_data.size(), sizeof(SessionTicketKey)));
  }

  keys.emplace_back();
  SessionTicketKey& dst_key = keys.back();

  std::copy_n(key_data.begin(), dst_key.name_.size(), dst_key.name_.begin());
  size_t pos = dst_key.name_.size();
  std::copy_n(key_data.begin() + pos, dst_key.hmac_key_.size(), dst_key.hmac_key_.begin());
  pos += dst_key.hmac_key_.size();
  std::copy_n(key_data.begin() + pos, dst_key.aes_key_.size(), dst_key.aes_key_.begin());
  pos += dst_key.aes_key_.size();
  ASSERT(key_data.begin() + pos == key_data.end());
}

} // namespace Ssl
} // namespace Envoy
