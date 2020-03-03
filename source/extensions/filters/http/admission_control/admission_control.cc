#include "extensions/filters/http/admission_control/admission_control.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/filter_config.h"

#include "common/common/assert.h"
#include "common/common/cleanup.h"
#include "common/common/enum_to_int.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

static constexpr double defaultAggression = 2.0;
static constexpr std::chrono::seconds defaultHistoryGranularity{1};

AdmissionControlFilterConfig::AdmissionControlFilterConfig(
    const AdmissionControlProto& proto_config, Runtime::Loader& runtime, TimeSource& time_source,
    Runtime::RandomGenerator& random, Stats::Scope& scope, ThreadLocal::SlotPtr&& tls)
    : runtime_(runtime), time_source_(time_source), random_(random), scope_(scope),
      tls_(std::move(tls)), admission_control_feature_(proto_config.enabled(), runtime_),
      aggression_(
          proto_config.has_aggression_coefficient()
              ? std::make_unique<Runtime::Double>(proto_config.aggression_coefficient(), runtime_)
              : nullptr) {
}

double AdmissionControlFilterConfig::aggression() const {
  return std::max<double>(1.0, aggression_ ? aggression_->value() : defaultAggression);
}

AdmissionControlFilter::AdmissionControlFilter(AdmissionControlFilterConfigSharedPtr config,
                                               const std::string& stats_prefix)
    : config_(std::move(config)), stats_(generateStats(config_->scope(), stats_prefix)) {}

Http::FilterHeadersStatus AdmissionControlFilter::decodeHeaders(Http::RequestHeaderMap&, bool end_stream) {
  if (!end_stream || !config_->filterEnabled() || decoder_callbacks_->streamInfo().healthCheck()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (shouldRejectRequest()) {
    decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, "", nullptr, absl::nullopt,
                                       "denied by admission control");
    stats_.rq_rejected_.inc();
    return Http::FilterHeadersStatus::StopIteration;
  }

  deferred_sample_task_ =
      std::make_unique<Cleanup>([this]() { config_->getController().recordFailure(); });

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus AdmissionControlFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                                bool end_stream) {
  if (end_stream) {
    const uint64_t status_code = Http::Utility::getResponseStatus(headers);
    if (status_code < 500) {
      config_->getController().recordSuccess();
      deferred_sample_task_->cancel();
    } else {
      deferred_sample_task_.reset();
    }
  }
  return Http::FilterHeadersStatus::Continue;
}

bool AdmissionControlFilter::shouldRejectRequest() const {
  const double total = config_->getController().requestTotalCount();
  const double success = config_->getController().requestSuccessCount();
  const double probability = (total - config_->aggression() * success) / (total + 1);

  // Choosing an accuracy of 4 significant figures for the probability.
  static constexpr uint64_t accuracy = 1e4;
  auto r = config_->random().random();
  return (accuracy * std::max(probability, 0.0)) > (r % accuracy);
}

ThreadLocalControllerImpl::ThreadLocalControllerImpl(TimeSource& time_source,
                                             std::chrono::seconds sampling_window)
    : time_source_(time_source), sampling_window_(sampling_window) {}

void ThreadLocalControllerImpl::maybeUpdateHistoricalData() {
  const MonotonicTime now = time_source_.monotonicTime();

  // Purge stale samples.
  while (!historical_data_.empty() && (now - historical_data_.front().first) >= sampling_window_) {
    global_data_.successes -= historical_data_.front().second.successes;
    global_data_.requests -= historical_data_.front().second.requests;
    historical_data_.pop_front();
  }

  // It's possible we purged stale samples from the history and are left with nothing, so it's
  // necessary to add an empty entry. We will also need to roll over into a new entry in the
  // historical data if we've exceeded the time specified by the granularity.
  if (historical_data_.empty() ||
      (now - historical_data_.back().first) >= defaultHistoryGranularity) {
    historical_data_.emplace_back(time_source_.monotonicTime(), RequestData());
  }
}

void ThreadLocalControllerImpl::recordRequest(const bool success) {
  maybeUpdateHistoricalData();

  // The back of the deque will be the most recent samples.
  ++historical_data_.back().second.requests;
  ++global_data_.requests;
  if (success) {
    ++historical_data_.back().second.successes;
    ++global_data_.successes;
  }
}

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
