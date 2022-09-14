#include "source/extensions/filters/http/custom_response/config.h"

#include <memory>

#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

namespace {

struct CustomResponseNameAction : public Matcher::ActionBase<ProtobufWkt::StringValue> {
  explicit CustomResponseNameAction(ResponseSharedPtr response) : response_(response) {}
  const ResponseSharedPtr response_;
};

using CustomResponseActionFactoryContext =
    absl::flat_hash_map<absl::string_view, ResponseSharedPtr>;

class CustomResponseNameActionFactory
    : public Matcher::ActionFactory<CustomResponseActionFactoryContext>,
      Logger::Loggable<Logger::Id::config> {
public:
  Matcher::ActionFactoryCb createActionFactoryCb(const Protobuf::Message& config,
                                                 CustomResponseActionFactoryContext& responses,
                                                 ProtobufMessage::ValidationVisitor&) override {
    ResponseSharedPtr response = nullptr;
    const auto& name = dynamic_cast<const ProtobufWkt::StringValue&>(config);
    const auto response_match = responses.find(name.value());
    if (response_match != responses.end()) {
      response = response_match->second;
    } else {
      ENVOY_LOG(debug, "matcher API points to an absent custom response '{}'", name.value());
    }
    return [response = std::move(response)]() {
      return std::make_unique<CustomResponseNameAction>(response);
    };
  }
  std::string name() const override { return "custom_response_name"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
};

REGISTER_FACTORY(CustomResponseNameActionFactory,
                 Matcher::ActionFactory<CustomResponseActionFactoryContext>);

class CustomResponseNameActionValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Http::HttpMatchingData> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<Http::HttpMatchingData>&,
                                          absl::string_view) override {
    return absl::OkStatus();
  }
};

} // namespace
FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
    Stats::StatName stats_prefix, Server::Configuration::FactoryContext& context)
    : FilterConfigBase(config, context.getServerFactoryContext()),
      stat_names_(context.scope().symbolTable()),
      stats_(stat_names_, context.scope(), stats_prefix) {}

FilterConfigBase::FilterConfigBase(
    const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
    Server::Configuration::ServerFactoryContext& context) {
  for (const auto& source : config.custom_responses()) {
    auto source_ptr = std::make_shared<Response>(source, context);
    if (source_ptr->name().empty()) {
      throw EnvoyException("name cannot be empty");
    }
    if (responses_.contains(source_ptr->name())) {
      throw EnvoyException("name needs to be unique");
    }
    responses_.emplace(source_ptr->name(), std::move(source_ptr));
  }
  if (config.has_custom_response_matcher()) {
    CustomResponseNameActionValidationVisitor validation_visitor;
    Matcher::MatchTreeFactory<Http::HttpMatchingData, CustomResponseActionFactoryContext> factory(
        responses_, context, validation_visitor);
    matcher_ = factory.create(config.custom_response_matcher())();
  } else {
    throw EnvoyException("matcher can not be unset");
  }
}

ResponseSharedPtr FilterConfigBase::getResponse(Http::ResponseHeaderMap& headers,
                                                const StreamInfo::StreamInfo& stream_info) const {
  if (!matcher_) {
    return ResponseSharedPtr{};
  }

  Http::Matching::HttpMatchingDataImpl data(stream_info.downstreamAddressProvider());
  data.onResponseHeaders(headers);
  auto match = Matcher::evaluateMatch<Http::HttpMatchingData>(*matcher_, data);
  if (!match.result_) {
    return ResponseSharedPtr{};
  }

  const auto result = match.result_();
  ASSERT(result->typeUrl() == CustomResponseNameAction::staticTypeUrl());
  ASSERT(dynamic_cast<CustomResponseNameAction*>(result.get()));
  return static_cast<const CustomResponseNameAction*>(result.get())->response_;
}

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
