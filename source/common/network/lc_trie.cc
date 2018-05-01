#include "common/network/lc_trie.h"

namespace Envoy {
namespace Network {
namespace LcTrie {

template <class IpType, uint32_t address_size>
const uint32_t LcTrie::LcTrieInternal<IpType, address_size>::MAXIMUM_CIDR_RANGE_ENTRIES;

LcTrie::LcTrie(const std::vector<std::pair<std::string, std::vector<Address::CidrRange>>>& tag_data,
               double fill_factor, uint32_t root_branching_factor) {
  BinaryTrie<Ipv4> ipv4_temp;
  BinaryTrie<Ipv6> ipv6_temp;
  for (const auto& pair_data : tag_data) {
    for (const auto& cidr_range : pair_data.second) {
      if (cidr_range.ip()->version() == Address::IpVersion::v4) {
        IpPrefix<Ipv4> ip_prefix(ntohl(cidr_range.ip()->ipv4()->address()), cidr_range.length(),
                                 pair_data.first);
        ipv4_temp.insert(ip_prefix);
      } else {
        IpPrefix<Ipv6> ip_prefix(Utility::Ip6ntohl(cidr_range.ip()->ipv6()->address()),
                                 cidr_range.length(), pair_data.first);
        ipv6_temp.insert(ip_prefix);
      }
    }
  }

  std::vector<IpPrefix<Ipv4>> ipv4_prefixes = ipv4_temp.push_leaves();
  ipv4_trie_.reset(new LcTrieInternal<Ipv4>(ipv4_prefixes, fill_factor, root_branching_factor));

  std::vector<IpPrefix<Ipv6>> ipv6_prefixes = ipv6_temp.push_leaves();
  ipv6_trie_.reset(new LcTrieInternal<Ipv6>(ipv6_prefixes, fill_factor, root_branching_factor));
}

std::vector<std::string>
LcTrie::getTags(const Network::Address::InstanceConstSharedPtr& ip_address) const {
  if (ip_address->ip()->version() == Address::IpVersion::v4) {
    Ipv4 ip = ntohl(ip_address->ip()->ipv4()->address());
    return ipv4_trie_->getTags(ip);
  } else {
    Ipv6 ip = Utility::Ip6ntohl(ip_address->ip()->ipv6()->address());
    return ipv6_trie_->getTags(ip);
  }
}

} // namespace LcTrie
} // namespace Network
} // namespace Envoy
