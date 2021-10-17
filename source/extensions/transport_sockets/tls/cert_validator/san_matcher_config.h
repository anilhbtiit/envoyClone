#pragma once

#include <memory>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.pb.h"
#include "envoy/ssl/certificate_validation_context_config.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/common/hash.h"
#include "source/common/common/matchers.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/transport_sockets/tls/cert_validator/default_validator.h"
#include "source/extensions/transport_sockets/tls/utility.h"

#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

template <int general_name_type> class StringSanMatcher : public Envoy::Ssl::SanMatcher {
public:
  bool match(const GENERAL_NAMES* general_names) const override {
    for (const GENERAL_NAME* general_name : general_names) {
      if (general_name->type == general_name_type &&
          DefaultCertValidator::verifySubjectAltName(general_name, matcher_)) {
        return true;
      }
    }
    return false;
  }

  ~StringSanMatcher() override = default;

  StringSanMatcher(envoy::type::matcher::v3::StringMatcher matcher) : matcher_(matcher) {}

private:
  Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher> matcher_;
};

using DnsSanMatcher = StringSanMatcher<GEN_DNS>;
using EmailSanMatcher = StringSanMatcher<GEN_EMAIL>;
using UriSanMatcher = StringSanMatcher<GEN_URI>;
using IpAddSanMatcher = StringSanMatcher<GEN_IPADD>;

class BackwardsCompatibleSanMatcher : public Envoy::Ssl::SanMatcher {

public:
  bool match(const GENERAL_NAMES* general_names) const override;

  ~BackwardsCompatibleSanMatcher() override = default;

  BackwardsCompatibleSanMatcher(envoy::type::matcher::v3::StringMatcher matcher)
      : matcher_(matcher) {}

private:
  Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher> matcher_;
};

class BackwardsCompatibleSanMatcherFactory : public Envoy::Ssl::SanMatcherFactory {
public:
  ~BackwardsCompatibleSanMatcherFactory() override = default;
  Envoy::Ssl::SanMatcherPtr
  createSanMatcher(const envoy::config::core::v3::TypedExtensionConfig* config) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher>();
  }
  std::string name() const override { return "envoy.san_matchers.backward_compatible_san_matcher"; }
};

Envoy::Ssl ::SanMatcherPtr createBackwardsCompatibleSanMatcher(
    envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher const& matcher);

Envoy::Ssl::SanMatcherPtr createStringSanMatcher(
    envoy::extensions::transport_sockets::tls::v3::StringSanMatcher const& matcher);
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
