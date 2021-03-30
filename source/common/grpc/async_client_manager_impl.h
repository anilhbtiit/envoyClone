#pragma once

#include <unordered_map>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/singleton/manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/grpc/stat_names.h"

namespace Envoy {
namespace Grpc {

class AsyncClientFactoryImpl : public AsyncClientFactory {
public:
  AsyncClientFactoryImpl(Upstream::ClusterManager& cm,
                         const envoy::config::core::v3::GrpcService& config,
                         bool skip_cluster_check, TimeSource& time_source);

  RawAsyncClientPtr create() override;

private:
  Upstream::ClusterManager& cm_;
  const envoy::config::core::v3::GrpcService config_;
  TimeSource& time_source_;
};

class GoogleAsyncClientFactoryImpl : public AsyncClientFactory {
public:
  GoogleAsyncClientFactoryImpl(ThreadLocal::Instance& tls, ThreadLocal::Slot* google_tls_slot,
                               Stats::Scope& scope,
                               const envoy::config::core::v3::GrpcService& config, Api::Api& api,
                               const StatNames& stat_names);

  RawAsyncClientPtr create() override;

private:
  ThreadLocal::Instance& tls_;
  ThreadLocal::Slot* google_tls_slot_;
  Stats::ScopeSharedPtr scope_;
  const envoy::config::core::v3::GrpcService config_;
  Api::Api& api_;
  const StatNames& stat_names_;
};

class AsyncClientManagerImpl : public AsyncClientManager {
public:
  AsyncClientManagerImpl(Upstream::ClusterManager& cm, ThreadLocal::Instance& tls,
                         TimeSource& time_source, Api::Api& api, const StatNames& stat_names);

  // Grpc::AsyncClientManager
  AsyncClientFactoryPtr factoryForGrpcService(const envoy::config::core::v3::GrpcService& config,
                                              Stats::Scope& scope,
                                              bool skip_cluster_check) override;

  RawAsyncClientSharedPtr
  getOrCreateRawAsyncClient(const envoy::config::core::v3::GrpcService& config, Stats::Scope& scope,
                            bool skip_cluster_check) override {
    RawAsyncClientSharedPtr client;
    client = thread_local_cache_->getCache(config);
    if (client != nullptr) {
      return client;
    }
    client = factoryForGrpcService(config, scope, skip_cluster_check)->create();
    thread_local_cache_->setCache(config, client);
    return client;
  }

private:
  class ThreadLocalCache : public ThreadLocal::ThreadLocalObject {
  public:
    void setCache(const envoy::config::core::v3::GrpcService& config,
                  RawAsyncClientSharedPtr client) {
      const std::uint64_t key = MessageUtil::hash(config.google_grpc());
      cache_[key] = client;
    }
    RawAsyncClientSharedPtr getCache(const envoy::config::core::v3::GrpcService& config) {
      const std::uint64_t key = MessageUtil::hash(config.google_grpc());
      auto it = cache_.find(key);
      if (it == cache_.end()) {
        return nullptr;
      }
      return cache_[key];
    }

  private:
    absl::flat_hash_map<uint64_t, RawAsyncClientSharedPtr> cache_;
  };
  Upstream::ClusterManager& cm_;
  ThreadLocal::Instance& tls_;
  ThreadLocal::SlotPtr google_tls_slot_;
  TimeSource& time_source_;
  Api::Api& api_;
  const StatNames& stat_names_;
  ThreadLocal::TypedSlot<ThreadLocalCache> thread_local_cache_;
};

} // namespace Grpc
} // namespace Envoy
