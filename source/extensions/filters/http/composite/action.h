#pragma once

#include "envoy/extensions/filters/http/composite/v3/composite.pb.validate.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

class ExecuteFilterAction
    : public Matcher::ActionBase<
          envoy::extensions::filters::http::composite::v3::ExecuteFilterAction> {
public:
  explicit ExecuteFilterAction(Http::FilterFactoryCb cb) : cb_(std::move(cb)) {}

  void createFilters(Http::FilterChainFactoryCallbacks& callbacks) const;

private:
  Http::FilterFactoryCb cb_;
};

class ExecuteFilterActionFactory
    : public Logger::Loggable<Logger::Id::filter>,
      public Matcher::ActionFactory<Http::Matching::HttpFilterActionContext> {
public:
  std::string name() const override { return "composite-action"; }
  Matcher::ActionFactoryCb
  createActionFactoryCb(const Protobuf::Message& config,
                        Http::Matching::HttpFilterActionContext& context,
                        ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& composite_action = MessageUtil::downcastAndValidate<
        const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction&>(
        config, validation_visitor);

    auto& factory =
        Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
            composite_action.typed_config());
    ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
        composite_action.typed_config().typed_config(), validation_visitor, factory);

    Envoy::Http::FilterFactoryCb callback = nullptr;

    // First, try to create the filter factory creation function from factory context (if exists).
    if (context.factory_context_.has_value()) {
      callback = factory.createFilterFactoryFromProto(*message, context.stat_prefix_,
                                                      context.factory_context_.value());
    }

    // If above failed, try to create the filter factory creation function from server factory
    // context (if exists).
    if (callback == nullptr && context.server_factory_context_.has_value()) {
      callback = factory.createFilterFactoryFromProtoWithServerContext(
          *message, context.stat_prefix_, context.server_factory_context_.value());
    }

    RELEASE_ASSERT(callback != nullptr, "Failed to get filter factory creation function.");

    return [cb = std::move(callback)]() -> Matcher::ActionPtr {
      return std::make_unique<ExecuteFilterAction>(cb);
    };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::http::composite::v3::ExecuteFilterAction>();
  }
};

DECLARE_FACTORY(ExecuteFilterActionFactory);

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
