#pragma once

#include "envoy/extensions/filters/common/set_filter_state/v3/rule.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace SetFilterState {

using LifeSpan = StreamInfo::FilterState::LifeSpan;
using StateType = StreamInfo::FilterState::StateType;
using StreamSharing = StreamInfo::StreamSharingMayImpactPooling;

struct Rule {
  std::string key_;
  StreamInfo::FilterState::ObjectFactory* factory_;
  StateType state_type_{StateType::ReadOnly};
  StreamSharing stream_sharing_{StreamSharing::None};
  bool skip_if_empty_;
  Formatter::FormatterConstSharedPtr value_;
};

class Config : public Logger::Loggable<Logger::Id::config> {
public:
  Config(const Protobuf::RepeatedPtrField<
             envoy::extensions::filters::common::set_filter_state::v3::Rule>& proto_rules,
         LifeSpan life_span, Server::Configuration::CommonFactoryContext& context)
      : life_span_(life_span), rules_(parse(proto_rules, context)) {}
  void updateFilterState(const Formatter::HttpFormatterContext& context,
                         StreamInfo::StreamInfo& info) const;

private:
  std::vector<Rule>
  parse(const Protobuf::RepeatedPtrField<
            envoy::extensions::filters::common::set_filter_state::v3::Rule>& proto_rules,
        Server::Configuration::CommonFactoryContext& context) const;
  const LifeSpan life_span_;
  const std::vector<Rule> rules_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

} // namespace SetFilterState
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
