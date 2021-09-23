#include "source/extensions/filters/common/rbac/matchers/upstream_ip.h"

#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/stream_info/upstream_address.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Matchers {

using namespace Filters::Common::RBAC;

UpstreamIpMatcher::UpstreamIpMatcher(
    const envoy::extensions::rbac::matchers::upstream_ip::v3::UpstreamIpMatcher& proto)
    : range_(Network::Address::CidrRange::create(proto.upstream_ip())) {
  if (proto.has_upstream_port_range()) {
    port_ = proto.upstream_port_range();
  }
}

bool UpstreamIpMatcher::matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
                                const StreamInfo::StreamInfo& info) const {

  if (!info.filterState().hasDataWithName(StreamInfo::UpstreamAddress::key())) {
    ENVOY_LOG_EVERY_POW_2(
        warn,
        "Did not find filter state with key: {}. Do you have a filter in the filter chain "
        "before the RBAC filter which populates the filter state with upstream addresses ?",
        StreamInfo::UpstreamAddress::key());

    return false;
  }

  const StreamInfo::UpstreamAddress& address_obj =
      info.filterState().getDataReadOnly<StreamInfo::UpstreamAddress>(
          StreamInfo::UpstreamAddress::key());

  bool is_match = false;
  if (range_.isInRange(*address_obj.address_)) {
    ENVOY_LOG(debug, "UpstreamIp matcher for range: {} evaluated to: true", range_.asString());
	is_match = true;
  }

  if (is_match && port_) {
    const auto port = address_obj.address_->ip()->port();
    if (port >= port_->start() && port <= port_->end()) {
      ENVOY_LOG(debug, "UpstreamIp matcher matched port: {}", port);
    } else {
        is_match = false;
    }
  }

  ENVOY_LOG(trace, "UpstreamIp matcher evaluated to: {}", is_match);
  return is_match;
}

REGISTER_FACTORY(UpstreamIpMatcherFactory, MatcherExtensionFactory);

} // namespace Matchers
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
