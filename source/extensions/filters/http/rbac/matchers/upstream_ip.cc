#include "source/extensions/filters/http/rbac/matchers/upstream_ip.h"

#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/stream_info/address_set_accessor_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {
namespace Matcher {

using namespace Filters::Common::RBAC;

bool UpstreamIpMatcher::matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
                                const StreamInfo::StreamInfo& info) const {

  if (!info.filterState().hasDataWithName(StreamInfo::AddressSetAccessorImpl::key())) {
    ENVOY_LOG_MISC(warn, "Did not find dynamic forward proxy metadata. Do you have dynamic "
                         "forward proxy in the filter chain before the RBAC filter ?");
    return false;
  }

  bool ipMatch = false;

  const StreamInfo::AddressSetAccessor& address_set =
      info.filterState().getDataReadOnly<StreamInfo::AddressSetAccessor>(
          StreamInfo::AddressSetAccessorImpl::key());

  address_set.iterate([&, this](const Network::Address::InstanceConstSharedPtr& address) {
    ipMatch = range_.isInRange(*address.get());
    if (ipMatch) {
      ENVOY_LOG_MISC(debug, "Address {} matched range: {}", address->asString(), range_.asString());
      return false;
    }

    return true;
  });

  ENVOY_LOG_MISC(debug, "UpstreamIp matcher for range: {} evaluated to: {}", range_.asString(),
                 ipMatch);
  return ipMatch;
}

REGISTER_FACTORY(UpstreamIpMatcherFactory, MatcherExtensionFactory);

} // namespace Matcher
} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
