#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include "source/common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

void RateLimitClientImpl::onReceiveMessage(RateLimitQuotaResponsePtr&&) {}

void RateLimitClientImpl::onRemoteClose(Grpc::Status::GrpcStatus status,
                                        const std::string& message) {
  stream_closed = true;
  if (status == Grpc::Status::Ok) {
    ENVOY_LOG(debug, "gRPC stream closed remotely with OK status {}: {}", status, message);
  } else {
    ENVOY_LOG(error, "gRPC stream closed remotely with error status {}: {}", status, message);
  }
}

void RateLimitClientImpl::createReports(
    envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports&) {}

// TODO(tyxia) Key of performing the rate limiting
void RateLimitClientImpl::rateLimit() {
  // TODO(tyxia) Do we need this assert at all?
  // How to coordinate with the `startStream` function which create the stream_
  ASSERT(stream_ != nullptr);
  // TODO(tyxia) workflow of rateLimit()
  // 1. Build buckets based on request attributes
  // 2. retrieve quota assignment
  // 3. report usage
  // 2 and 3 are in parallel.

  envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
  // TODO(tyxia) This could be wrapped into a utility function.
  // stream_.sendMessage(std::move(reports), true);
  send(std::move(reports), true);
}

void RateLimitClientImpl::send(
    envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports&& reports, bool end_stream) {
  stream_.sendMessage(std::move(reports), end_stream);
}

bool RateLimitClientImpl::startStream() {
  // Starts stream if it has not been opened yet.
  if (stream_ == nullptr) {
    Http::AsyncClient::StreamOptions options;
    stream_ = aync_client_.start(
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.rate_limit_quota.v3.RateLimitQuotaService.StreamRateLimitQuotas"),
        *this, options);

    if (stream_ == nullptr) {
      ENVOY_LOG(error, "Unable to establish the new stream");
      return false;
      // TODO(tyxia) Error handling
      // re-try or other kinds of error handling actions
    }
  }
  return true;
}

void RateLimitClientImpl::closeStream() {
  // Close the stream if it is in open state.
  // TODO(tyxia) onRemoteClose will set this flag to false; Avoid the double close??
  if (stream_ != nullptr && !stream_closed) {
    stream_->closeStream();
    stream_closed = true;
    stream_->resetStream();
  }
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
