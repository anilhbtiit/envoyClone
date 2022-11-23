#include "source/common/upstream/deterministic_aperture_lb.h"

#include "envoy/config/cluster/v3/cluster.pb.h"

namespace Envoy {
namespace Upstream {

envoy::config::cluster::v3::Cluster::RingHashLbConfig toClusterRingHashLbConfig(
    const envoy::extensions::load_balancing_policies::ring_hash::v3::RingHash& ring_hash_config) {
  envoy::config::cluster::v3::Cluster::RingHashLbConfig return_value;

  return_value.set_hash_function(
      static_cast<envoy::config::cluster::v3::Cluster::RingHashLbConfig::HashFunction>(
          ring_hash_config.hash_function()));

  return_value.mutable_minimum_ring_size()->CopyFrom(ring_hash_config.minimum_ring_size());
  return_value.mutable_maximum_ring_size()->CopyFrom(ring_hash_config.maximum_ring_size());

  return return_value;
}

DeterministicApertureLoadBalancer::DeterministicApertureLoadBalancer(
    const PrioritySet& priority_set, ClusterLbStats& stats, Stats::Scope& scope,
    Runtime::Loader& runtime, Random::RandomGenerator& random,
    const absl::optional<envoy::extensions::load_balancing_policies::deterministic_aperture::v3::
                             DeterministicApertureLbConfig>& config,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
    : RingHashLoadBalancer(priority_set, stats, scope, runtime, random,
                           ((config.has_value() && config->has_ring_config())
                            : absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig>(
                                toClusterRingHashLbConfig(config->ring_config()))
                            : absl::nullopt),
                           common_config),
      width_((config.has_value() && config->total_peers() > 0) ? (1.0 / config->total_peers())
                                                               : 1.0),
      offset_((config.has_value() && width_ > 0.0) ? (width_ * config->peer_index()) : 0.0),
      scope_(scope.createScope("deterministic_aperture_lb.")),
      ring_stats_(RingHashLoadBalancer::generateStats(*scope_)) {}

DeterministicApertureLoadBalancerRingStats
DeterministicApertureLoadBalancer::Ring::generateStats(Stats::Scope& scope) {
  return {ALL_DETERMINISTIC_APERTURE_LOAD_BALANCER_RING_STATS(POOL_COUNTER(scope))};
}

/*
 * TODO(jojy): The concept of HashingLoadBalancer::chooseHost might not directly apply here since we
 * don't actually use the hash of the nodes placed in the ring. We use `random` algorithm in the
 * Deterministic aperture load balancer. Hence we ignore the `h` and `attempt` here.
 */
HostConstSharedPtr DeterministicApertureLoadBalancer::Ring::chooseHost(uint64_t, uint32_t) const {
  if (ring_.empty()) {
    return nullptr;
  }

  const std::pair<size_t, size_t> index_pair = pick2();

  const RingHashLoadBalancer::RingEntry& first = ring_[index_pair.first];
  const RingHashLoadBalancer::RingEntry& second = ring_[index_pair.second];

  ENVOY_LOG(debug, "pick2 returned hosts: (hash1: {}, address1: {}, hash2: {}, address2: {})",
            first.hash_, first.host_->address()->asString(), second.hash_,
            second.host_->address()->asString());

  return first.host_->stats().rq_active_.value() < second.host_->stats().rq_active_.value()
             ? first.host_
             : second.host_;
}

using HashFunction = envoy::config::cluster::v3::Cluster::RingHashLbConfig::HashFunction;
DeterministicApertureLoadBalancer::Ring::Ring(
    double offset, double width, const NormalizedHostWeightVector& normalized_host_weights,
    double min_normalized_weight, uint64_t min_ring_size, uint64_t max_ring_size,
    HashFunction hash_function, bool use_hostname_for_hashing, Stats::ScopeSharedPtr scope,
    RingHashLoadBalancerStats ring_stats)
    : RingHashLoadBalancer::Ring(normalized_host_weights, min_normalized_weight, min_ring_size,
                                 max_ring_size, hash_function, use_hostname_for_hashing,
                                 ring_stats),
      offset_(offset), width_(width), unit_width_(1.0 / ring_size_), rng_(random_dev_()),
      random_distribution_(0, 1), stats_(generateStats(*scope)) {
  if (width_ > 1.0 || width_ < 0) {
    throw EnvoyException(
        fmt::format("Invalid width for the deterministic aperture ring{}", width_));
  }
}

absl::optional<double> DeterministicApertureLoadBalancer::Ring::weight(size_t index, double offset,
                                                                       double width) const {
  if (index >= ring_size_ || width > 1 || offset > 1) {
    return absl::nullopt;
  }

  double index_begin = index * unit_width_;
  double index_end = index_begin + unit_width_;

  if (offset + width > 1) {
    double start = std::fmod((offset + width), 1.0);

    return 1.0 - (intersect(index_begin, index_end, start, offset)) / unit_width_;
  }

  return intersect(index_begin, index_end, offset, offset + width) / unit_width_;
}

size_t DeterministicApertureLoadBalancer::Ring::getIndex(double offset) const {
  ASSERT(offset >= 0 && offset <= 1, "valid offset");
  return offset / unit_width_;
}

double DeterministicApertureLoadBalancer::Ring::intersect(double b0, double e0, double b1,
                                                          double e1) const {
  ENVOY_LOG(trace, "Overlap for (b0: {}, e0: {}, b1: {}, e1: {})", b0, e0, b1, e1);
  return std::max(0.0, std::min(e0, e1) - std::max(b0, b1));
}

size_t DeterministicApertureLoadBalancer::Ring::pick() const {
  return getIndex(std::fmod((offset_ + width_ * nextRandom()), 1.0));
}

size_t DeterministicApertureLoadBalancer::Ring::pickSecond(size_t first) const {
  double f_begin = first * unit_width_;
  ENVOY_LOG(trace, "Pick second for (first: {}, offset: {}, width: {}, first begin: {})", first,
            offset_, width_, f_begin);

  if (f_begin + 1 < offset_ + width_) {
    f_begin = f_begin + 1;
    ENVOY_LOG(trace, "Adjusted first begin to : {}", f_begin);
  }

  double f_end = f_begin + unit_width_;

  double overlap = intersect(f_begin, f_end, offset_, offset_ + width_);
  double rem = width_ - overlap;

  if (rem <= 0) {
    return first;
  }

  double pos = offset_ + (nextRandom() * rem);
  ENVOY_LOG(trace, "Overlap: {}, remainder: {}, second offset: {}", overlap, rem, pos);

  if (pos >= (f_end - overlap)) {
    pos += overlap;
    ENVOY_LOG(trace, "Adjusted second offset to: {}", pos);
  }

  return getIndex(std::fmod(pos, 1.0));
}

std::pair<size_t, size_t> DeterministicApertureLoadBalancer::Ring::pick2() const {
  ENVOY_LOG(trace, "pick2 for offset: {}, width: {}", offset_, width_);
  const size_t first = pick();
  const size_t second = pickSecond(first);

  if (first == second) {
    stats_.pick2_same_.inc();
  }

  ENVOY_LOG(trace, "Returning: ({}, {})", first, second);
  return {first, second};
}

ThreadAwareLoadBalancerPtr DeterministicApertureLoadBalancerFactory::create(
    const ClusterInfo& cluster_info, const PrioritySet& priority_set, Runtime::Loader& runtime,
    Random::RandomGenerator& random, TimeSource& time_source) {
  (void)time_source;
  return std::make_unique<DeterministicApertureLoadBalancer>(
      priority_set, cluster_info.lbStats(), cluster_info.statsScope(), runtime, random,
      cluster_info.lbDeterministicApertureConfig(), cluster_info.lbConfig());
}

REGISTER_FACTORY(DeterministicApertureLoadBalancerFactory, Upstream::TypedLoadBalancerFactory);

} // namespace Upstream
} // namespace Envoy
