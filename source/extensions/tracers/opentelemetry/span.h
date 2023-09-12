#pragma once

#include "envoy/api/api.h"
#include "envoy/config/trace/v3/opentelemetry.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/trace_driver.h"

#include "source/common/common/logger.h"
#include "source/extensions/tracers/common/factory_base.h"
#include "source/extensions/tracers/opentelemetry/grpc_trace_exporter.h"

#include "absl/strings/escaping.h"
#include "span_context.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class Tracer;

/**
 * OpenTelemetry tracing implementation of the Envoy Span object.
 * Note that it has a pointer to its parent Tracer to access the shared Exporter.
 */
class Span : Logger::Loggable<Logger::Id::tracing>, public Tracing::Span {
public:
  Span(const Tracing::Config& config, const std::string& name, SystemTime start_time,
       Envoy::TimeSource& time_source, Tracer& parent_tracer);

  // Tracing::Span functions
  void setOperation(absl::string_view /*operation*/) override{};
  void setTag(absl::string_view /*name*/, absl::string_view /*value*/) override;
  void log(SystemTime /*timestamp*/, const std::string& /*event*/) override{};
  void finishSpan() override;
  void injectContext(Envoy::Tracing::TraceContext& /*trace_context*/,
                     const Upstream::HostDescriptionConstSharedPtr&) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config& config, const std::string& name,
                              SystemTime start_time) override;

  /**
   * Set the span's sampled flag.
   */
  void setSampled(bool sampled) override { sampled_ = sampled; };

  /**
   * @return whether or not the sampled attribute is set
   */

  bool sampled() const { return sampled_; }

  std::string getBaggage(absl::string_view /*key*/) override { return EMPTY_STRING; };
  void setBaggage(absl::string_view /*key*/, absl::string_view /*value*/) override{};

  // Additional methods

  /**
   * Sets the span's trace id attribute.
   */
  void setTraceId(const absl::string_view& trace_id_hex) {
    span_.set_trace_id(absl::HexStringToBytes(trace_id_hex));
  }

  std::string getTraceIdAsHex() const override { return absl::BytesToHexString(span_.trace_id()); };

  /**
   * Sets the span's id.
   */
  void setId(const absl::string_view& span_id_hex) {
    span_.set_span_id(absl::HexStringToBytes(span_id_hex));
  }

  std::string spanId() { return absl::BytesToHexString(span_.span_id()); }

  /**
   * Sets the span's parent id.
   */
  void setParentId(const absl::string_view& parent_span_id_hex) {
    span_.set_parent_span_id(absl::HexStringToBytes(parent_span_id_hex));
  }

  std::string tracestate() { return span_.trace_state(); }

  /**
   * Sets the span's tracestate.
   */
  void setTracestate(const absl::string_view& tracestate) {
    span_.set_trace_state(std::string{tracestate});
  }

private:
  ::opentelemetry::proto::trace::v1::Span span_;
  Tracer& parent_tracer_;
  Envoy::TimeSource& time_source_;
  bool sampled_;
};

using SpanPtr = std::unique_ptr<Span>;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
