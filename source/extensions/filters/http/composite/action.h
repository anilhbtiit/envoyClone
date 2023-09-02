#pragma once

#include "envoy/extensions/filters/http/composite/v3/composite.pb.validate.h"

#include "source/common/http/filter_chain_helper.h"
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
  ~ExecuteFilterAction() { std::cout << "ExecuteFilterAction destructor" << std::endl; }

  void createFilters(Http::FilterChainFactoryCallbacks& callbacks) const;

private:
  Http::FilterFactoryCb cb_;
};

class ExecuteFilterActionFactory
    : public Logger::Loggable<Logger::Id::filter>,
      public Matcher::ActionFactory<Http::Matching::HttpFilterActionContext> {
public:
  ~ExecuteFilterActionFactory() {
    std::cout << "ExecuteFilterActionFactory destructor" << std::endl;
  }
  std::string name() const override { return "composite-action"; }

  Matcher::ActionFactoryCb
  createActionFactoryCb(const Protobuf::Message& config,
                        Http::Matching::HttpFilterActionContext& context,
                        ProtobufMessage::ValidationVisitor& validation_visitor) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::http::composite::v3::ExecuteFilterAction>();
  }

private:
  std::shared_ptr<Http::DownstreamFilterConfigProviderManager> filter_config_provider_manager_;
  std::unique_ptr<
      Envoy::Filter::DynamicFilterConfigProvider<Envoy::Filter::NamedHttpFilterFactoryCb>>
      provider_;
};

DECLARE_FACTORY(ExecuteFilterActionFactory);

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
