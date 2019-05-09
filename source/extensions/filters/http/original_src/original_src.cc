#include "extensions/filters/http/original_src/original_src.h"

#include "common/common/assert.h"

#include "extensions/filters/common/original_src/socket_option_factory.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OriginalSrc {

OriginalSrcFilter::OriginalSrcFilter(const Config& config) : config_(config) {}

void OriginalSrcFilter::onDestroy() {}

Http::FilterHeadersStatus OriginalSrcFilter::decodeHeaders(Http::HeaderMap&, bool) {
  const auto downstream_address = callbacks_->streamInfo().downstreamRemoteAddress();
  ASSERT(downstream_address);

  ENVOY_LOG(debug,
            "Got a new connection in the original_src filter for address {}. Marking with {}",
            downstream_address->asString(), config_.mark());

  if (downstream_address->type() != Network::Address::Type::Ip) {
    // Nothing we can do with this.
    return Http::FilterHeadersStatus::Continue;
  }

  const auto options_to_add = Filters::Common::OriginalSrc::buildOriginalSrcOptions(
      std::move(downstream_address), config_.mark());
  callbacks_->addUpstreamSocketOptions(options_to_add);
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus OriginalSrcFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus OriginalSrcFilter::decodeTrailers(Http::HeaderMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void OriginalSrcFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

} // namespace OriginalSrc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
