#include "source/extensions/outlier_detection_monitors/common/monitor_base_impl.h"

namespace Envoy {
namespace Extensions {
namespace Outlier {

bool HTTPCodesBucket::matches(
    const TypedExtResult<Upstream::Outlier::ExtResultType::HTTP_CODE>& result) const {
  // We should not get here with errors other then HTTP codes.
  ASSERT(result.type() == ExtResultType::HTTP_CODE);
  const HttpCode& http_code = dynamic_cast<const HttpCode&>(result);
  return ((http_code.code() >= start_) && (http_code.code() <= end_));
}

bool LocalOriginEventsBucket::matches(
    const TypedExtResult<Upstream::Outlier::ExtResultType::LOCAL_ORIGIN>& event) const {
  // We should not get here with errors other then Local Origin events.
  ASSERT(event.type() == ExtResultType::LOCAL_ORIGIN);
  const LocalOriginEvent& local_origin_event = dynamic_cast<const LocalOriginEvent&>(event);
  // Capture all events except the success
  return (!((local_origin_event.result() == Result::LocalOriginConnectSuccessFinal) ||
            (local_origin_event.result() == Result::ExtOriginRequestSuccess)));
}

void Monitor::reportResult(const ExtResult& result) {
  if (buckets_.empty()) {
    return;
  }

  bool matchedError = false;
  bool matchedType = false;
  // iterate over all error buckets
  for (auto& bucket : buckets_) {
    // if the bucket is not interested in this type of result/error
    // just ignore it.
    if (!bucket->matchType(result)) {
      continue;
    }

    matchedType = true;

    // check if the bucket "catches" the result.
    if (bucket->match(result)) {
      matchedError = true;
      break;
    }
  }

  // If none of buckets had the matching type, just bail out.
  if (!matchedType) {
    return;
  }

  if (matchedError) {
    // Count as error.
    if (onError()) {
      callback_(enforce_, name(), absl::nullopt);
      // Reaching error was reported via callback.
      // but the host may or may not be ejected based on enforce_ parameter.
      // Reset the monitor's state, so a single new error does not
      // immediately trigger error condition again.
      onReset();
    }
  } else {
    onSuccess();
  }
}

void Monitor::processBucketsConfig(
    const envoy::extensions::outlier_detection_monitors::common::v3::ErrorBuckets& config) {
  for (const auto& http_bucket : config.http_errors()) {
    addErrorBucket(
        std::make_unique<HTTPCodesBucket>(http_bucket.range().start(), http_bucket.range().end()));
  }
  for (auto i = 0; i < config.local_origin_errors().size(); i++) {
    addErrorBucket(std::make_unique<LocalOriginEventsBucket>());
  }
}
} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
