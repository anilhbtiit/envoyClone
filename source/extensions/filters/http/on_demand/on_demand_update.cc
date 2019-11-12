#include "extensions/filters/http/on_demand/on_demand_update.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

void OnDemandRouteUpdate::requestRouteConfigUpdate() {
  if (callbacks_->route() != nullptr || !callbacks_->canRequestRouteConfigUpdate()) {
    filter_return_ = FilterReturn::ContinueDecoding;
  } else {
    callbacks_->requestRouteConfigUpdate([this]() -> void { onRouteConfigUpdateCompletion(); });
    filter_return_ = FilterReturn::StopDecoding;
  }
}

Http::FilterHeadersStatus OnDemandRouteUpdate::decodeHeaders(Http::HeaderMap&, bool) {
  requestRouteConfigUpdate();
  return filter_return_ == FilterReturn::StopDecoding ? Http::FilterHeadersStatus::StopIteration
                                                      : Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus OnDemandRouteUpdate::decodeData(Buffer::Instance&, bool) {
  return filter_return_ == FilterReturn::StopDecoding
             ? Http::FilterDataStatus::StopIterationAndWatermark
             : Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus OnDemandRouteUpdate::decodeTrailers(Http::HeaderMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void OnDemandRouteUpdate::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

// This is the callback which is called when an update requested in requestRouteConfigUpdate()
// has been propagated to workers, at which point the request processing is restarted from the
// beginning.
void OnDemandRouteUpdate::onRouteConfigUpdateCompletion() {
  filter_return_ = FilterReturn::ContinueDecoding;

  if (callbacks_->canResolveRouteAfterConfigUpdate() && // route can be resolved after an on-demand
                                                        // VHDS update
      !callbacks_->decodingBuffer() &&                  // Redirects with body not yet supported.
      callbacks_->recreateStream()) {
    // cluster_->stats().upstream_internal_redirect_succeeded_total_.inc();
    return;
  }

  // route cannot be resolved after an on-demand VHDS update or
  // recreating stream failed, continue the filter-chain
  callbacks_->continueDecoding();
}

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
