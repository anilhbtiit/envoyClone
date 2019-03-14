#pragma once

#include "common/upstream/conn_pool_map.h"

namespace Envoy {
namespace Upstream {

template <typename KEY_TYPE, typename POOL_TYPE>
ConnPoolMap<KEY_TYPE, POOL_TYPE>::ConnPoolMap(Envoy::Event::Dispatcher& dispatcher,
                                              HostConstSharedPtr host, ResourcePriority priority)
    : thread_local_dispatcher_(dispatcher), host_(host), priority_(priority) {}

template <typename KEY_TYPE, typename POOL_TYPE> ConnPoolMap<KEY_TYPE, POOL_TYPE>::~ConnPoolMap() {
  // Explicitly clear things out for resource tracking purposes. Note that we call this rather than
  // clear because in this case we don't want to do a deferred delete.
  clearActivePools();
}

template <typename KEY_TYPE, typename POOL_TYPE>
typename ConnPoolMap<KEY_TYPE, POOL_TYPE>::OptPoolRef
ConnPoolMap<KEY_TYPE, POOL_TYPE>::getPool(KEY_TYPE key, const PoolFactory& factory) {
  Common::AutoDebugRecursionChecker assert_not_in(recursion_checker_);
  // TODO(klarose): Consider how we will change the connection pool's configuration in the future.
  // The plan is to change the downstream socket options... We may want to take those as a parameter
  // here. Maybe we'll pass them to the factory function?
  auto pool_iter = active_pools_.find(key);
  if (pool_iter != active_pools_.end()) {
    return std::ref(*(pool_iter->second));
  }

  // We need a new pool. Check if we have room.
  if (!host_->cluster().resourceManager(priority_).connectionPools().canCreate()) {
    // We're full. Try to free up a pool. If we can't, bail out.
    if (!freeOnePool()) {
      return absl::nullopt;
    }

    // TODO(klarose): Consider some simple hysteresis here. How can we prevent iterating over all
    // pools when we're at the limit every time we want to allocate a new one, even if most of the
    // pools are not busy, while balancing that with not unnecessarily freeing all pools? If we
    // start freeing once we cross a threshold, then stop after we cross another, we could
    // achieve that balance.
  }

  // We have room for a new pool. Allocate one and let it know about any cached callbacks.
  auto new_pool = factory();
  host_->cluster().resourceManager(priority_).connectionPools().inc();
  for (const auto& cb : cached_callbacks_) {
    new_pool->addDrainedCallback(cb);
  }

  auto inserted = active_pools_.emplace(key, std::move(new_pool));
  return std::ref(*inserted.first->second);
}

template <typename KEY_TYPE, typename POOL_TYPE>
size_t ConnPoolMap<KEY_TYPE, POOL_TYPE>::size() const {
  return active_pools_.size();
}

template <typename KEY_TYPE, typename POOL_TYPE> void ConnPoolMap<KEY_TYPE, POOL_TYPE>::clear() {
  Common::AutoDebugRecursionChecker assert_not_in(recursion_checker_);
  for (auto& pool_pair : active_pools_) {
    thread_local_dispatcher_.deferredDelete(std::move(pool_pair.second));
  }
  clearActivePools();
}

template <typename KEY_TYPE, typename POOL_TYPE>
void ConnPoolMap<KEY_TYPE, POOL_TYPE>::addDrainedCallback(const DrainedCb& cb) {
  Common::AutoDebugRecursionChecker assert_not_in(recursion_checker_);
  for (auto& pool_pair : active_pools_) {
    pool_pair.second->addDrainedCallback(cb);
  }

  cached_callbacks_.emplace_back(std::move(cb));
}

template <typename KEY_TYPE, typename POOL_TYPE>
void ConnPoolMap<KEY_TYPE, POOL_TYPE>::drainConnections() {
  Common::AutoDebugRecursionChecker assert_not_in(recursion_checker_);
  for (auto& pool_pair : active_pools_) {
    pool_pair.second->drainConnections();
  }
}

template <typename KEY_TYPE, typename POOL_TYPE>
bool ConnPoolMap<KEY_TYPE, POOL_TYPE>::freeOnePool() {
  // Try to find a pool that isn't doing anything.
  auto pool_iter = active_pools_.begin();
  while (pool_iter != active_pools_.end()) {
    if (!pool_iter->second->hasActiveConnections()) {
      break;
    }
    ++pool_iter;
  }

  if (pool_iter != active_pools_.end()) {
    // We found one. Free it up, and let the caller know.
    thread_local_dispatcher_.deferredDelete(std::move(pool_iter->second));
    active_pools_.erase(pool_iter);
    host_->cluster().resourceManager(priority_).connectionPools().dec();
    return true;
  }

  return false;
}

template <typename KEY_TYPE, typename POOL_TYPE>
void ConnPoolMap<KEY_TYPE, POOL_TYPE>::clearActivePools() {
  host_->cluster().resourceManager(priority_).connectionPools().decBy(active_pools_.size());
  active_pools_.clear();
}
} // namespace Upstream
} // namespace Envoy
