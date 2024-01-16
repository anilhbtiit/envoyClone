#include "source/extensions/http/original_ip_detection/xff/xff.h"

#include "source/common/http/utility.h"
#include "source/common/network/cidr_range.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace Xff {

XffIPDetection::XffIPDetection(
    const envoy::extensions::http::original_ip_detection::xff::v3::XffConfig& config)
    : xff_num_trusted_hops_(!config.has_xff_trusted_cidrs() ? config.xff_num_trusted_hops() : 0),
      recurse_(config.xff_trusted_cidrs().recurse().value()) {
  xff_trusted_cidrs_.reserve(config.xff_trusted_cidrs().cidrs().size());
  for (const envoy::config::core::v3::CidrRange& entry : config.xff_trusted_cidrs().cidrs()) {
    Network::Address::CidrRange cidr = Network::Address::CidrRange::create(entry);
    xff_trusted_cidrs_.push_back(cidr);
  }
}

XffIPDetection::XffIPDetection(uint32_t xff_num_trusted_hops)
    : xff_num_trusted_hops_(xff_num_trusted_hops), recurse_(false) {}

Envoy::Http::OriginalIPDetectionResult
XffIPDetection::detect(Envoy::Http::OriginalIPDetectionParams& params) {
  if (!xff_trusted_cidrs_.empty()) {
    if (!Envoy::Http::Utility::remoteAddressIsTrustedProxy(params.downstream_remote_address,
                                                           xff_trusted_cidrs_)) {
      return {nullptr, false, absl::nullopt};
    }
    if (recurse_) {
      // Check XFF for last IP that isn't in `xff_trusted_cidrs`
      auto ret = Envoy::Http::Utility::getLastNonTrustedAddressFromXFF(params.request_headers,
                                                                       xff_trusted_cidrs_);
      return {ret.address_, ret.allow_trusted_address_checks_, absl::nullopt};
    }
  }

  auto ret =
      Envoy::Http::Utility::getLastAddressFromXFF(params.request_headers, xff_num_trusted_hops_);
  return {ret.address_, ret.allow_trusted_address_checks_, absl::nullopt};
}

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
