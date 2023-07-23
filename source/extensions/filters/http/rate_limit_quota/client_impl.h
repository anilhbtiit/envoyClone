#pragma once
#include <memory>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"

#include "source/common/grpc/typed_async_client.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/rate_limit_quota/client.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ::envoy::service::rate_limit_quota::v3::BucketId;
using ::envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports;
using BucketQuotaUsage =
    ::envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports::BucketQuotaUsage;
using GrpcAsyncClient =
    Grpc::AsyncClient<envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports,
                      envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>;

// Grpc bidirectional streaming client which handles the communication with RLS server.
class RateLimitClientImpl : public RateLimitClient,
                            public Grpc::AsyncStreamCallbacks<
                                envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>,
                            public Logger::Loggable<Logger::Id::rate_limit_quota> {
public:
  RateLimitClientImpl(const envoy::config::core::v3::GrpcService& grpc_service,
                      Server::Configuration::FactoryContext& context,
                      // TODO(tyxia) life time, filter itself destroyed but client is
                      // stored in the cached. need to outlived filter object!!
                      RateLimitQuotaCallbacks& callbacks, BucketsContainer& quota_buckets,
                      RateLimitQuotaUsageReports& usage_reports)
      : aync_client_(context.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
            grpc_service, context.scope(), true)),
        callbacks_(callbacks), quota_buckets_(quota_buckets), reports_(usage_reports) {}

  void onReceiveMessage(RateLimitQuotaResponsePtr&& response) override;

  // RawAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  // RateLimitClient
  // TODO(tyxia) remove. sendUsageReport() accomplish the work
  void rateLimit(RateLimitQuotaCallbacks&) override{};

  absl::Status startStream(const StreamInfo::StreamInfo& stream_info) override;
  void closeStream();

  // Build the usage report (i.e., the request sent to RLQS server).
  RateLimitQuotaUsageReports buildUsageReport(absl::string_view domain, const BucketId& bucket_id);
  // Send the usage report to RLQS server
  void sendUsageReport(absl::string_view domain, absl::optional<BucketId> bucket_id);

  void setStreamClosed() { stream_closed_ = true; }

private:
  // Store the client as the bare object since there is no ownership transfer involved.
  GrpcAsyncClient aync_client_;
  Grpc::AsyncStream<RateLimitQuotaUsageReports> stream_{};

  // TODO(tyxia) Further look at the use of this flag later.
  bool stream_closed_ = false;

  // TODO(tyxia) if the response is from periodical report, then the filter object is possible that
  // it is not the old filter anymore, how we do onQuotaResponse and update the treadLocal storage.
  // The TLS should be same but filter is different now how about storage it in the TLS then??? How
  // about the client and filter class have the pointer points to the same TLS object Then we don't
  // even need the callback to update it then!!!
  RateLimitQuotaCallbacks& callbacks_;
  // Don't take ownership here and these objects are stored in TLS.
  BucketsContainer& quota_buckets_;
  RateLimitQuotaUsageReports& reports_;
};

using RateLimitClientPtr = std::unique_ptr<RateLimitClientImpl>;
/**
 * Create the rate limit client. It is uniquely owned by each worker thread.
 */
inline RateLimitClientPtr
createRateLimitClient(Server::Configuration::FactoryContext& context,
                      const envoy::config::core::v3::GrpcService& grpc_service,
                      RateLimitQuotaCallbacks& callbacks, BucketsContainer& quota_buckets,
                      RateLimitQuotaUsageReports& quota_usage_reports) {
  return std::make_unique<RateLimitClientImpl>(grpc_service, context, callbacks, quota_buckets,
                                               quota_usage_reports);
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
