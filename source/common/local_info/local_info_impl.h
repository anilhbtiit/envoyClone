#pragma once

#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/local_info/local_info.h"
#include "envoy/stats/symbol_table.h"

#include "common/config/version_converter.h"

namespace Envoy {
namespace LocalInfo {

class LocalInfoImpl : public LocalInfo {
public:
  LocalInfoImpl(Stats::SymbolTable& symbol_table, const envoy::config::core::v3::Node& node,
                const Network::Address::InstanceConstSharedPtr& address,
                absl::string_view zone_name, absl::string_view cluster_name,
                absl::string_view node_name)
      : node_(node), address_(address), zone_stat_name_storage_(zone_name, symbol_table),
        zone_stat_name_(zone_stat_name_storage_.statName()) {
    if (!zone_name.empty()) {
      node_.mutable_locality()->set_zone(std::string(zone_name));
    }
    if (!cluster_name.empty()) {
      node_.set_cluster(std::string(cluster_name));
    }
    if (!node_name.empty()) {
      node_.set_id(std::string(node_name));
    }
  }

  Network::Address::InstanceConstSharedPtr address() const override { return address_; }
  const std::string& zoneName() const override { return node_.locality().zone(); }
  const Stats::StatName& zoneStatName() const override { return zone_stat_name_; }
  const std::string& clusterName() const override { return node_.cluster(); }
  const std::string& nodeName() const override { return node_.id(); }
  const envoy::config::core::v3::Node& node() const override { return node_; }

private:
  envoy::config::core::v3::Node node_;
  Network::Address::InstanceConstSharedPtr address_;
  const Stats::StatNameManagedStorage zone_stat_name_storage_;
  const Stats::StatName zone_stat_name_;
};

} // namespace LocalInfo
} // namespace Envoy
