#pragma once
#include <algorithm>
#include <memory>

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.validate.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ::envoy::service::rate_limit_quota::v3::BucketId;
using ::envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports;

using BucketAction = ::envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse::BucketAction;
using BucketQuotaUsage =
    ::envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports::BucketQuotaUsage;

// Forward declaration
class RateLimitClientImpl;

// Customized hash and equal struct for `BucketId` hash key.
struct BucketIdHash {
  size_t operator()(const BucketId& bucket_id) const { return MessageUtil::hash(bucket_id); }
};

struct BucketIdEqual {
  bool operator()(const BucketId& id1, const BucketId& id2) const {
    return Protobuf::util::MessageDifferencer::Equals(id1, id2);
  }
};

// Single bucket entry
struct Bucket {
  // TODO(tyxia) This copy constructor is tricky
  // the unique ptr inside of this class and can only be moveable.
  // Bucket (const Bucket& elem) = default;
  // Default constructor
  Bucket() = default;
  // Default move constructor and assignment.
  Bucket(Bucket&& bucket) = default;
  Bucket& operator=(Bucket&& bucket) = default;

  virtual ~Bucket() = default;
  // TODO(tyxia) Each bucket owns the unique grpc client for sending the quota usage report
  // periodically.
  std::unique_ptr<RateLimitClientImpl> rate_limit_client;
  // The timer for sending the reports periodically.
  Event::TimerPtr send_reports_timer;
  // Cached bucket action from the response that was received from the RLQS server.
  BucketAction bucket_action;
  // TODO(tyxia) No need to store bucket ID  as it is already the key of BucketsContainer.
  BucketQuotaUsage quota_usage;
};

using BucketsContainer = absl::node_hash_map<BucketId, Bucket, BucketIdHash, BucketIdEqual>;

class ThreadLocalBucket : public Envoy::ThreadLocal::ThreadLocalObject {
public:
  // TODO(tyxia) Here is similar to defer initialization methodology.
  // We have the empty hash map in thread local storage at the beginning and build the map later
  // in the `rate_limit_quota_filter` when the request comes.
  ThreadLocalBucket() = default;

  // Return the buckets. Buckets are returned by reference so that caller site can modify its
  // content.
  BucketsContainer& buckets() { return buckets_; }
  // Return the quota usage reports.
  RateLimitQuotaUsageReports& quotaUsageReports() { return quota_usage_reports_; }

private:
  // Thread local storage for bucket container and quota usage report.
  BucketsContainer buckets_;
  RateLimitQuotaUsageReports quota_usage_reports_;
};

struct BucketCache {
  BucketCache(Envoy::Server::Configuration::FactoryContext& context) : tls(context.threadLocal()) {
    tls.set([](Envoy::Event::Dispatcher&) {
      // Unused
      // dispatcher.timeSource()
      return std::make_shared<ThreadLocalBucket>();
    });
  }
  Envoy::ThreadLocal::TypedSlot<ThreadLocalBucket> tls;
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
