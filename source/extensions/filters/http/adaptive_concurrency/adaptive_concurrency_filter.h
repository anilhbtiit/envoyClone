#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/time.h"
#include "envoy/config/filter/http/adaptive_concurrency/v2alpha/adaptive_concurrency.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/cleanup.h"

#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {

/**
 * Configuration for the adaptive concurrency limit filter.
 */
class AdaptiveConcurrencyFilterConfig {
public:
  AdaptiveConcurrencyFilterConfig(
      const envoy::config::filter::http::adaptive_concurrency::v2alpha::AdaptiveConcurrency&
          proto_config,
      Runtime::Loader& runtime, std::string stats_prefix, Stats::Scope& scope,
      TimeSource& time_source);

  // Returns whether the filter should operate as a pass-through for a request.
  bool filterDisabled() const {
    // RuntimeFeatureFlag proto validates that the runtime key must have at least a single
    // character. If the key is empty, the field was never specified.
    if (disabled_runtime_key_.empty()) {
      return false;
    }

    return runtime_.snapshot().getBoolean(disabled_runtime_key_, disabled_default_value_);
  }

  TimeSource& timeSource() const { return time_source_; }

private:
  const std::string stats_prefix_;
  Runtime::Loader& runtime_;
  TimeSource& time_source_;
  const std::string disabled_runtime_key_;
  const bool disabled_default_value_;
};

using AdaptiveConcurrencyFilterConfigSharedPtr =
    std::shared_ptr<const AdaptiveConcurrencyFilterConfig>;
using ConcurrencyControllerSharedPtr =
    std::shared_ptr<ConcurrencyController::ConcurrencyController>;

/**
 * A filter that samples request latencies and dynamically adjusts the request
 * concurrency window.
 */
class AdaptiveConcurrencyFilter : public Http::PassThroughFilter,
                                  Logger::Loggable<Logger::Id::filter> {
public:
  AdaptiveConcurrencyFilter(AdaptiveConcurrencyFilterConfigSharedPtr config,
                            ConcurrencyControllerSharedPtr controller);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override;

  // Http::StreamEncoderFilter
  void encodeComplete() override;
  void onDestroy() override;

private:
  AdaptiveConcurrencyFilterConfigSharedPtr config_;
  const ConcurrencyControllerSharedPtr controller_;
  std::unique_ptr<Cleanup> deferred_sample_task_;
};

} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
