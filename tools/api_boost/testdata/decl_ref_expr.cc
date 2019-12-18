#include "envoy/api/v2/cds.pb.h"
#include "envoy/config/overload/v2alpha/overload.pb.h"

#define ASSERT(x) static_cast<void>(x)

using envoy::config::overload::v2alpha::Trigger;

using envoy::api::v2::Cluster;
using MutableStringClusterAccessor = std::string* (Cluster::*)();

class ThresholdTriggerImpl {
public:
  ThresholdTriggerImpl(const envoy::config::overload::v2alpha::Trigger& config) {
    switch (config.trigger_oneof_case()) {
    case envoy::config::overload::v2alpha::Trigger::kThreshold:
      break;
    default:
      break;
    }
    switch (config.trigger_oneof_case()) {
    case Trigger::kThreshold:
      break;
    default:
      break;
    }
    envoy::config::overload::v2alpha::ThresholdTrigger::default_instance();
    ASSERT(envoy::config::overload::v2alpha::Trigger::kThreshold == Trigger::kThreshold);
    ASSERT(Foo::kThreshold == Trigger::kThreshold);
    envoy::api::v2::Cluster_LbPolicy_Name(0);
    static_cast<void>(envoy::api::v2::Cluster::MAGLEV);
    MutableStringClusterAccessor foo = &envoy::api::v2::Cluster::mutable_name;
    static_cast<void>(foo);
  }

  using Foo = envoy::config::overload::v2alpha::Trigger;
};
