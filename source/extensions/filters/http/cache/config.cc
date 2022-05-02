#include "source/extensions/filters/http/cache/config.h"

#include "source/extensions/filters/http/cache/cache_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

Http::FilterFactoryCb CacheFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  const std::string type{TypeUtil::typeUrlToDescriptorFullName(config.typed_config().type_url())};
  HttpCacheFactory* const http_cache_factory =
      Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(type);
  if (http_cache_factory == nullptr) {
    throw EnvoyException(
        fmt::format("Didn't find a registered implementation for type: '{}'", type));
  }

  // Capture the cache instance as a reference; the factory must own it.
  // TODO: getCache should be returning a shared_ptr (and using SingletonManager), to avoid
  // keeping potentially large structures in static variables.
  auto cache = std::ref(http_cache_factory->getCache(config, context));

  return [config, stats_prefix, &context,
          cache](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<CacheFilter>(config, stats_prefix, context.scope(),
                                                            context.timeSource(), cache));
  };
}

REGISTER_FACTORY(CacheFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
