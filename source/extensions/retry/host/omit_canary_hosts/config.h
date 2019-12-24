#include "envoy/upstream/retry.h"

#include "extensions/retry/host/omit_canary_hosts/omit_canary_hosts.h"
#include "extensions/retry/host/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

class OmitCanaryHostsRetryPredicateFactory : public Upstream::RetryHostPredicateFactory {

public:
  Upstream::RetryHostPredicateSharedPtr createHostPredicate(const Protobuf::Message&,
                                                            uint32_t) override {
    return std::make_shared<OmitCanaryHostsRetryPredicate>();
  }

  const std::string name() const override {
    return RetryHostPredicateValues::get().OmitCanaryHostsPredicate;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Envoy::ProtobufWkt::Empty>();
  }
};

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
