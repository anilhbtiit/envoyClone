#pragma once

#include <functional>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/ssl/certificate_validation_context_config.h"
#include "envoy/ssl/tls_certificate_config.h"
#include "envoy/ssl/trusted_ca_config.h"

namespace Envoy {
namespace Secret {

class TlsCertificateConfigProviderImpl : public TlsCertificateConfigProvider {
public:
  TlsCertificateConfigProviderImpl(const envoy::api::v2::auth::TlsCertificate& tls_certificate);

  const Ssl::TlsCertificateConfig* secret() const override { return tls_certificate_.get(); }

  Common::CallbackHandle* addUpdateCallback(std::function<void()>) override { return nullptr; }

private:
  Ssl::TlsCertificateConfigPtr tls_certificate_;
};

class CertificateValidationContextConfigProviderImpl
    : public CertificateValidationContextConfigProvider {
public:
  CertificateValidationContextConfigProviderImpl(
      const envoy::api::v2::auth::CertificateValidationContext& certificate_validation_context);

  const Ssl::CertificateValidationContextConfig* secret() const override {
    return certificate_validation_context_.get();
  }

  Common::CallbackHandle* addUpdateCallback(std::function<void()>) override { return nullptr; }

private:
  Ssl::CertificateValidationContextConfigPtr certificate_validation_context_;
};

class TrustedCaConfigProviderImpl : public TrustedCaConfigProvider {
public:
  TrustedCaConfigProviderImpl(const envoy::api::v2::core::DataSource& trusted_ca);

  const Ssl::TrustedCaConfig* secret() const override {
    return trusted_ca_.get();
  }

  Common::CallbackHandle* addUpdateCallback(std::function<void()>) override { return nullptr; }

private:
  Ssl::TrustedCaConfigPtr trusted_ca_;
};

} // namespace Secret
} // namespace Envoy
