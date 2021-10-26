#include "source/extensions/filters/udp/udp_proxy/router/router_impl.h"

#include "envoy/extensions/filters/udp/udp_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/route.pb.validate.h"
#include "envoy/type/matcher/v3/network_inputs.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/network/matching/data_impl.h"
#include "source/common/network/matching/inputs.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace Router {

Matcher::ActionFactoryCb RouteMatchActionFactory::createActionFactoryCb(
    const Protobuf::Message& config, RouteActionContext&,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& route_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::udp::udp_proxy::v3::Route&>(config, validation_visitor);
  const auto& cluster = route_config.cluster();

  return [cluster]() { return std::make_unique<RouteMatchAction>(cluster); };
}

REGISTER_FACTORY(RouteMatchActionFactory, Matcher::ActionFactory<RouteActionContext>);

absl::Status RouteActionValidationVisitor::performDataInputValidation(
    const Matcher::DataInputFactory<Network::Matching::NetworkMatchingData>&,
    absl::string_view type_url) {
  static std::string source_ip_input_name = TypeUtil::descriptorFullNameToTypeUrl(
      envoy::type::matcher::v3::SourceIpMatchInput::descriptor()->full_name());
  if (type_url == source_ip_input_name) {
    return absl::OkStatus();
  }

  return absl::InvalidArgumentError(
      fmt::format("Route table can only match on source IP, saw {}", type_url));
}

RouterImpl::RouterImpl(const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig& config,
                       Server::Configuration::ServerFactoryContext& factory_context) {
  if (config.has_cluster()) {
    cluster_ = config.cluster();
    entries_.push_back(config.cluster());
  } else {
    RouteActionContext context{};
    RouteActionValidationVisitor validation_visitor;
    Matcher::MatchTreeFactory<Network::Matching::NetworkMatchingData, RouteActionContext> factory(
        context, factory_context, validation_visitor);
    matcher_ = factory.create(config.matcher())();
  }
}

const std::string& RouterImpl::route(Network::Address::InstanceConstSharedPtr address) const {
  if (cluster_.has_value()) {
    return cluster_.value();
  } else {
    Network::Address::CidrRange cidr = Network::Address::CidrRange::create(address, address->ip()->ipv4() ? 32 : 128);
    Network::Matching::NetworkMatchingDataImpl data;
    data.onSourceIp(cidr);

    auto result = matcher_->match(data);
    if (result.match_state_ == Matcher::MatchState::MatchComplete) {
      if (result.on_match_.has_value()) {
        return result.on_match_.value().action_cb_()->getTyped<RouteMatchAction>().cluster();
      }
    }

    return EMPTY_STRING;
  }
}

const std::vector<std::string>& RouterImpl::entries() const { return entries_; }

} // namespace Router
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
