#include "common/ssl/certificate_validation_context_config_impl.h"

#include "envoy/common/exception.h"

#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/config/datasource.h"

namespace Envoy {
namespace Ssl {

static const std::string INLINE_STRING = "<inline>";

CertificateValidationContextConfigImpl::CertificateValidationContextConfigImpl(
    const envoy::api::v2::auth::CertificateValidationContext& config)
    : certificate_revocation_list_(Config::DataSource::read(config.crl(), true)),
      certificate_revocation_list_path_(
          Config::DataSource::getPath(config.crl())
              .value_or(certificate_revocation_list_.empty() ? EMPTY_STRING : INLINE_STRING)),
      verify_subject_alt_name_list_(config.verify_subject_alt_name().begin(),
                                    config.verify_subject_alt_name().end()),
      verify_certificate_hash_list_(config.verify_certificate_hash().begin(),
                                    config.verify_certificate_hash().end()),
      verify_certificate_spki_list_(config.verify_certificate_spki().begin(),
                                    config.verify_certificate_spki().end()),
      allow_expired_certificate_(config.allow_expired_certificate()) {
}

} // namespace Ssl
} // namespace Envoy
