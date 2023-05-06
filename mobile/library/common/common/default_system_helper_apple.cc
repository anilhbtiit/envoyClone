#include "library/common/common/default_system_helper.h"
#include "library/common/network/apple_platform_cert_verifier.h"

namespace Envoy {

bool DefaultSystemHelper::isCleartextPermitted(absl::string_view /*hostname*/) {
  return true;
}

envoy_cert_validation_result DefaultSystemHelper::validateCertificateChain(const envoy_data* certs, uint8_t size,
                                                      const char* hostname) {
  return verify_cert(certs, size, hostname);
}

void DefaultSystemHelper::cleanupAfterCertificateValidation() {}


} // namespace Envoy
