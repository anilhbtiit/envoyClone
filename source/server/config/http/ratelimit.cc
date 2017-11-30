#include "server/config/http/ratelimit.h"

#include <chrono>
#include <string>

#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/http/filter/ratelimit.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb
RateLimitFilterConfig::createFilter(const envoy::api::v2::filter::http::RateLimit& config,
                                    const std::string&, FactoryContext& context) {
  Http::RateLimit::FilterConfigSharedPtr filter_config(new Http::RateLimit::FilterConfig(
      config, context.localInfo(), context.scope(), context.runtime(), context.clusterManager()));
  const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(config, timeout, 20);
  return [filter_config, timeout_ms,
          &context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr{new Http::RateLimit::Filter(
        filter_config, context.rateLimitClient(std::chrono::milliseconds(timeout_ms)))});
  };
}

HttpFilterFactoryCb RateLimitFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                               const std::string& stats_prefix,
                                                               FactoryContext& context) {
  envoy::api::v2::filter::http::RateLimit config;
  Config::FilterJson::translateHttpRateLimitFilter(json_config, config);
  return createFilter(config, stats_prefix, context);
}

HttpFilterFactoryCb RateLimitFilterConfig::createFilterFactoryFromProto(
    const Protobuf::Message& config, const std::string& stats_prefix, FactoryContext& context) {
  return createFilter(dynamic_cast<const envoy::api::v2::filter::http::RateLimit&>(config),
                      stats_prefix, context);
}

/**
 * Static registration for the rate limit filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RateLimitFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
