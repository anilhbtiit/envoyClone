#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/certificate_providers/static_cert_provider/v3/config.pb.h"

#include "source/common/common/callback_impl.h"
#include "source/extensions/certificate_providers/factory.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {

class StaticCertificateProvider : public CertificateProvider::CertificateProvider,
                                  public CertificateProvider::CertificateSubscriptionCallbacks {
public:
  StaticCertificateProvider(const envoy::config::core::v3::TypedExtensionConfig& config,
                            Api::Api& api);

  Capabilites capabilities() const override { return capabilities_; };

  const std::string& caCert(absl::string_view /*cert_name*/) const override { return trust_ca_; };

  std::list<Envoy::CertificateProvider::Certpair> certPairs(absl::string_view cert_name,
                                                            bool generate) override;

  Common::CallbackHandlePtr addUpdateCallback(absl::string_view cert_name,
                                              std::function<void()> callback) override;

  void onCertpairsUpdated(absl::string_view cert_name,
                          std::list<Envoy::CertificateProvider::Certpair> certpairs) override;
  void onCACertUpdated(absl::string_view /*cert_name*/, const std::string /*cert*/) override{};
  void onUpatedFail() override{};

  void generateCertpair(absl::string_view cert_name);

private:
  Capabilites capabilities_;
  std::string trust_ca_;
  absl::flat_hash_map<absl::string_view, std::list<Envoy::CertificateProvider::Certpair>>
      cert_pairs_;
  absl::flat_hash_map<absl::string_view, Common::CallbackManager<>> update_callback_managers_;
};

class StaticCertificateProviderFactory : public CertificateProviderFactory {
public:
  StaticCertificateProviderFactory() = default;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::certificate_providers::static_cert_provider::v3::
            StaticCertificateProviderConfig()};
  }

  Envoy::CertificateProvider::CertificateProviderSharedPtr
  createCertificateProviderInstance(const envoy::config::core::v3::TypedExtensionConfig& config,
                                    Api::Api& api) override {
    return std::make_shared<StaticCertificateProvider>(config, api);
  }

  std::string name() const override { return "envoy.certificate_providers.static_cert_provider"; }
};

REGISTER_FACTORY(StaticCertificateProviderFactory, CertificateProviderFactory);

} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
