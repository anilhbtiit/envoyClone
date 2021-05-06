#pragma once

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"

#include "extensions/filters/network/redis_proxy/feature/hotkey/cache/lfucache/lfu_cache.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace Feature {
namespace HotKey {
namespace Cache {

class CacheFactory {
public:
  static CacheSharedPtr createCache(const envoy::extensions::filters::network::redis_proxy::v3::
                                        RedisProxy_FeatureConfig_HotKey::CacheType& type,
                                    const uint8_t& capacity, const uint8_t& warming_capacity = 5) {
    CacheSharedPtr ret(nullptr);
    switch (type) {
    case envoy::extensions::filters::network::redis_proxy::v3::
        RedisProxy_FeatureConfig_HotKey_CacheType_LFU:
    default:
      ret = std::make_shared<LFUCache::LFUCache>(capacity, warming_capacity);
    }
    return ret;
  }
};

} // namespace Cache
} // namespace HotKey
} // namespace Feature
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
