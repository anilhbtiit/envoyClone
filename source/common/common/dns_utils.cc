#include "source/common/common/dns_utils.h"

#include "source/common/common/assert.h"
#include "source/common/network/utility.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace DnsUtils {

Network::DnsLookupFamily
getDnsLookupFamilyFromCluster(const envoy::config::cluster::v3::Cluster& cluster) {
  return getDnsLookupFamilyFromEnum(cluster.dns_lookup_family());
}

Network::DnsLookupFamily
getDnsLookupFamilyFromEnum(envoy::config::cluster::v3::Cluster::DnsLookupFamily family) {
  switch (family) {
  case envoy::config::cluster::v3::Cluster::V6_ONLY:
    return Network::DnsLookupFamily::V6Only;
  case envoy::config::cluster::v3::Cluster::V4_ONLY:
    return Network::DnsLookupFamily::V4Only;
  case envoy::config::cluster::v3::Cluster::AUTO:
    return Network::DnsLookupFamily::Auto;
  case envoy::config::cluster::v3::Cluster::V4_PREFERRED:
    return Network::DnsLookupFamily::V4Preferred;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

std::vector<Network::Address::InstanceConstSharedPtr>
generateAddressList(const std::list<Network::DnsResponse>& responses, uint32_t port) {
  std::vector<Network::Address::InstanceConstSharedPtr> addresses;
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.allow_multiple_dns_addresses")) {
    return addresses;
  }
  for (const auto& response : responses) {
    auto address = Network::Utility::getAddressWithPort(*(response.address_), port);
    if (address) {
      addresses.push_back(address);
    }
  }
  return addresses;
}

bool listChanged(const std::vector<Network::Address::InstanceConstSharedPtr>& list1,
                 const std::vector<Network::Address::InstanceConstSharedPtr>& list2) {
  if (list1.size() != list2.size()) {
    return true;
  }
  // TODO(alyssawilk) we shouldn't consider order to constitute a change here.
  for (size_t i = 0; i < list1.size(); ++i) {
    if (*list1[i] != *list2[i]) {
      return true;
    }
  }
  return false;
}

} // namespace DnsUtils
} // namespace Envoy
