#include "extensions/filters/http/ratelimit/config.h"

#include <chrono>
#include <string>

#include "envoy/config/filter/http/rate_limit/v2/rate_limit.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/common/ratelimit/ratelimit_impl.h"
#include "extensions/filters/http/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

Http::FilterFactoryCb RateLimitFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::rate_limit::v2::RateLimit& proto_config, const std::string&,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.domain().empty());
  FilterConfigSharedPtr filter_config(
      new FilterConfig(proto_config, context.localInfo(), context.scope(), context.runtime()));
  const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20);
  // TODO(ramaraochavali): Figure out how to get the registered name here and also see if this code
  // can be used across filters.
  ratelimit_service_config_ =
      context.singletonManager().getTyped<RateLimit::RateLimitServiceConfig>(
          "ratelimit_service_config_singleton_name", [] { return nullptr; });
  if (ratelimit_service_config_) {
    ratelimit_client_factory_ = std::make_unique<Filters::Common::RateLimit::GrpcFactoryImpl>(
        ratelimit_service_config_->config_, context.clusterManager().grpcAsyncClientManager(),
        context.scope());
  } else {
    ratelimit_client_factory_ = std::make_unique<Filters::Common::RateLimit::NullFactoryImpl>();
  }
  return [filter_config, timeout_ms, &context,
          this](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(
        filter_config,
        ratelimit_client_factory_->create(std::chrono::milliseconds(timeout_ms), context)));
  };
}

Http::FilterFactoryCb
RateLimitFilterConfig::createFilterFactory(const Json::Object& json_config,
                                           const std::string& stats_prefix,
                                           Server::Configuration::FactoryContext& context) {
  envoy::config::filter::http::rate_limit::v2::RateLimit proto_config;
  Config::FilterJson::translateHttpRateLimitFilter(json_config, proto_config);
  return createFilterFactoryFromProtoTyped(proto_config, stats_prefix, context);
}

/**
 * Static registration for the rate limit filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RateLimitFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
