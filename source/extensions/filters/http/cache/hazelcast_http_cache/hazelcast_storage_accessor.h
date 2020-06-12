#pragma once

#include "envoy/common/exception.h"

#include "absl/strings/str_format.h"
#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_cache_entry.h"

#include "hazelcast/client/HazelcastClient.h"
#include "hazelcast/client/IMap.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

/**
 * Abstraction for storage connections of the cache.
 *
 * @note Decoupled from the cache in favor of local storage implementations
 * or mocks to test the cache without running a real Hazelcast Instance.
 */
class StorageAccessor {
public:
  StorageAccessor() = default;

  virtual void putHeader(const int64_t map_key, const HazelcastHeaderEntry& value) PURE;
  virtual void putBody(const std::string& map_key, const HazelcastBodyEntry& value) PURE;
  virtual void putResponse(const int64_t map_key, const HazelcastResponseEntry& value) PURE;

  virtual HazelcastHeaderPtr getHeader(const int64_t map_key) PURE;
  virtual HazelcastBodyPtr getBody(const std::string& map_key) PURE;
  virtual HazelcastResponsePtr getResponse(const int64_t map_key) PURE;

  virtual void removeBodyAsync(const std::string& map_key) PURE;
  virtual void removeHeader(const int64_t map_key) PURE;

  virtual bool tryLock(const int64_t map_key, bool unified) PURE;
  virtual void unlock(const int64_t map_key, bool unified) PURE;

  virtual bool isRunning() const PURE;
  virtual std::string clusterName() const PURE;
  virtual std::string startInfo() const PURE;

  virtual void connect() PURE;
  virtual void disconnect() PURE;

  virtual ~StorageAccessor() = default;
};

class HazelcastHttpCache;
class HeaderMapEntryListener;

/**
 * Accessor to Hazelcast Cluster.
 *
 * The cache uses this accessor in the production code.
 */
class HazelcastClusterAccessor : public StorageAccessor {
public:
  HazelcastClusterAccessor(HazelcastHttpCache& cache, ClientConfig&& client_config,
                           const std::string& app_prefix, const uint64_t partition_size);

  void putHeader(const int64_t map_key, const HazelcastHeaderEntry& value) override;
  void putBody(const std::string& map_key, const HazelcastBodyEntry& value) override;
  void putResponse(const int64_t map_key, const HazelcastResponseEntry& value) override;

  HazelcastHeaderPtr getHeader(const int64_t map_key) override;
  HazelcastBodyPtr getBody(const std::string& map_key) override;
  HazelcastResponsePtr getResponse(const int64_t map_key) override;

  void removeBodyAsync(const std::string& map_key) override;
  void removeHeader(const int64_t map_key) override;

  bool tryLock(const int64_t map_key, bool unified) override;
  void unlock(const int64_t map_key, bool unified) override;

  bool isRunning() const override;
  std::string clusterName() const override;
  std::string startInfo() const override;

  void connect() override;
  void disconnect() override;

  ~HazelcastClusterAccessor() override = default;

private:
  friend class RemoteTestAccessor;

  /**
   * Generates a map name unique to the cache configuration.
   *
   * @note  Maps with the same key & value types are differentiated by their names in
   * Hazelcast cluster. Hence each plugin will connect to a map named with partition
   * size and app_prefix. When a cache connects to a cluster which already has an active
   * cache with different body_partition_size, this naming will prevent incompatibility
   * and separate these two caches in the Hazelcast cluster.
   */
  std::string constructMapName(const std::string& postfix, bool unified);

  /** Returns remote header cache proxy */
  IMap<int64_t, HazelcastHeaderEntry> getHeaderMap() {
    if (!hazelcast_client_) {
      throw EnvoyException("Hazelcast Client is not connected to a cluster.");
    }
    return hazelcast_client_->getMap<int64_t, HazelcastHeaderEntry>(header_map_name_);
  }

  /** Returns remote body cache proxy */
  IMap<std::string, HazelcastBodyEntry> getBodyMap() {
    if (!hazelcast_client_) {
      throw EnvoyException("Hazelcast Client is not connected to a cluster.");
    }
    return hazelcast_client_->getMap<std::string, HazelcastBodyEntry>(body_map_name_);
  }

  /** Returns remote response cache proxy */
  IMap<int64_t, HazelcastResponseEntry> getResponseMap() {
    if (!hazelcast_client_) {
      throw EnvoyException("Hazelcast Client is not connected to a cluster.");
    }
    return hazelcast_client_->getMap<int64_t, HazelcastResponseEntry>(response_map_name_);
  }

  std::unique_ptr<HazelcastClient> hazelcast_client_;
  std::unique_ptr<HeaderMapEntryListener> listener_;
  HazelcastHttpCache& cache_;

  // From HazelcastCacheConfig
  const std::string& app_prefix_;
  uint64_t partition_size_;

  ClientConfig client_config_;

  std::string body_map_name_;
  std::string header_map_name_;
  std::string response_map_name_;
};

using hazelcast::client::EntryEvent;

/**
 * HeaderMap listener to clean up orphan bodies of which header is evicted.
 *
 * @note This handler is kicked only when a header entry is evicted, i.e. max configured
 * size is reached on HeaderMap and then eviction is performed. On a TTL or idleTime based
 * expiration, this listener will not take an action since it should be handled by the
 * TTL/maxIdleTime configuration of BodyMap configured on the server side.
 */
class HeaderMapEntryListener
    : public hazelcast::client::EntryListener<int64_t, HazelcastHeaderEntry> {
public:
  HeaderMapEntryListener(HazelcastHttpCache& cache) : cache_(cache) {}
  void entryEvicted(const EntryEvent<int64_t, HazelcastHeaderEntry>& event) override;
  void entryAdded(const EntryEvent<int64_t, HazelcastHeaderEntry>&) override {}
  void entryRemoved(const EntryEvent<int64_t, HazelcastHeaderEntry>&) override {}
  void entryUpdated(const EntryEvent<int64_t, HazelcastHeaderEntry>&) override {}
  void entryExpired(const EntryEvent<int64_t, HazelcastHeaderEntry>&) override {}
  void entryMerged(const EntryEvent<int64_t, HazelcastHeaderEntry>&) override {}
  void mapEvicted(const MapEvent&) override {}
  void mapCleared(const MapEvent&) override {}

private:
  HazelcastHttpCache& cache_;
};

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
