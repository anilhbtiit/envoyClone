#include <chrono>
#include <string>

#include "envoy/event/timer.h"
#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/common.h"
#include "test/test_common/test_base.h"

namespace Envoy {

// A filter returns StopAllIterationAndBuffer for headers. The iteration continues after 5s.
class DecodeHeadersReturnStopAllFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "decode-headers-return-stop-all-filter";

  // Returns Http::FilterHeadersStatus::StopAllIterationAndBuffer for headers. Triggers a timer to
  // continue iteration after 5s.
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override {
    createTimerForContinue();
    return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool) override {
    // decodeData will only be called once after iteration resumes.
    EXPECT_EQ(data.length(), 70000);
    Buffer::OwnedImpl added_data("a");
    decoder_callbacks_->addDecodedData(added_data, false);
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    Buffer::OwnedImpl data("a");
    decoder_callbacks_->addDecodedData(data, false);
    return Http::FilterTrailersStatus::Continue;
  }

private:
  // Creates a timer to continue iteration after 5s.
  void createTimerForContinue() {
    delay_timer_ = decoder_callbacks_->dispatcher().createTimer(
        [this]() -> void { decoder_callbacks_->continueDecoding(); });
    delay_timer_->enableTimer(std::chrono::seconds(5));
  }

  Event::TimerPtr delay_timer_;
};

constexpr char DecodeHeadersReturnStopAllFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<DecodeHeadersReturnStopAllFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
