#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// A filter which resets the downstream stream when decoding headers.
class ResetFilter : public Http::PassThroughFilter {
public:
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    decoder_callbacks_->resetStream();
    return Http::FilterHeadersStatus::StopIteration;
  }
};

class ResetFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpDualFilterConfig {
public:
  ResetFilterConfig() : EmptyHttpDualFilterConfig("reset-stream-filter") {}
  Http::FilterFactoryCb createDualFilter(const std::string&,
                                         Server::Configuration::ServerFactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::ResetFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<ResetFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<ResetFilterConfig,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
