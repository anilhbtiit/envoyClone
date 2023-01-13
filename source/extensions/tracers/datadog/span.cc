#include "source/extensions/tracers/datadog/span.h"

#include <datadog/dict_writer.h>
#include <datadog/sampling_priority.h>
#include <datadog/span_config.h>
#include <datadog/trace_segment.h>

#include <string_view>
#include <utility>

#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/tracers/datadog/string_util.h"
#include "source/extensions/tracers/datadog/time_util.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

class TraceContextWriter : public dd::DictWriter {
  Tracing::TraceContext* context_;

public:
  explicit TraceContextWriter(Tracing::TraceContext& context) : context_(&context) {}

  void set(dd::StringView key, dd::StringView value) override { context_->setByKey(key, value); }
};

} // namespace

Span::Span(dd::Span&& span) : span_(std::move(span)), trace_id_hex_(hex(span_->trace_id())) {}

const dd::Optional<dd::Span>& Span::impl() const { return span_; }

void Span::setOperation(absl::string_view operation) {
  if (!span_) {
    return;
  }

  span_->set_name(operation);
}

void Span::setTag(absl::string_view name, absl::string_view value) {
  if (!span_) {
    return;
  }

  span_->set_tag(name, value);
}

void Span::log(SystemTime, const std::string&) {
  // Datadog spans don't have in-bound "events" or "logs".
}

void Span::finishSpan() { span_.reset(); }

void Span::injectContext(Tracing::TraceContext& trace_context,
                         const Upstream::HostDescriptionConstSharedPtr&) {
  if (!span_) {
    return;
  }

  TraceContextWriter writer{trace_context};
  span_->inject(writer);
}

Tracing::SpanPtr Span::spawnChild(const Tracing::Config&, const std::string& name,
                                  SystemTime start_time) {
  if (!span_) {
    // I don't expect this to happen. This means that `spawnChild` was called
    // after `finishSpan`.
    return std::make_unique<Tracing::NullSpan>();
  }

  // The OpenTracing implementation ignored the `Tracing::Config` argument,
  // so we will as well.
  dd::SpanConfig config;
  config.name = name;
  config.start = estimateTime(start_time);

  return std::make_unique<Span>(span_->create_child(config));
}

void Span::setSampled(bool sampled) {
  if (!span_) {
    return;
  }

  auto priority = int(sampled ? dd::SamplingPriority::USER_KEEP : dd::SamplingPriority::USER_DROP);
  span_->trace_segment().override_sampling_priority(priority);
}

std::string Span::getBaggage(absl::string_view) {
  // not implemented
  return std::string{};
}

void Span::setBaggage(absl::string_view, absl::string_view) {
  // not implemented
}

std::string Span::getTraceIdAsHex() const { return trace_id_hex_; }

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
