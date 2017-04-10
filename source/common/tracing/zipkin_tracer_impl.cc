#include "common/tracing/http_tracer_impl.h"
#include "common/tracing/zipkin_tracer_impl.h"

#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/http/headers.h"
#include "common/http/header_map_impl.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

#include "zipkin/zipkin_core_constants.h"

namespace Tracing {

ZipkinSpan::ZipkinSpan(Zipkin::Span& span) : span_(span) {}

void ZipkinSpan::finishSpan() { span_.finish(); }

void ZipkinSpan::setTag(const std::string& name, const std::string& value) {
  if (this->hasCSAnnotation()) {
    span_.setTag(name, value);
  }
}

bool ZipkinSpan::hasCSAnnotation() {
  auto annotations = span_.annotations();
  if (annotations.size() > 0) {
    return annotations[0].value() == Zipkin::ZipkinCoreConstants::CLIENT_SEND;
  }
  return false;
}

ZipkinDriver::TlsZipkinTracer::TlsZipkinTracer(Zipkin::Tracer tracer, ZipkinDriver& driver)
    : tracer_(tracer), driver_(driver) {}

ZipkinDriver::ZipkinDriver(const Json::Object& config, Upstream::ClusterManager& cluster_manager,
                           Stats::Store& stats, ThreadLocal::Instance& tls,
                           Runtime::Loader& runtime, const LocalInfo::LocalInfo& local_info)
    : cm_(cluster_manager),
      tracer_stats_{ZIPKIN_TRACER_STATS(POOL_COUNTER_PREFIX(stats, "tracing.zipkin."))}, tls_(tls),
      runtime_(runtime), local_info_(local_info), tls_slot_(tls.allocateSlot()) {

  Upstream::ThreadLocalCluster* cluster = cm_.get(config.getString("collector_cluster"));
  if (!cluster) {
    throw EnvoyException(fmt::format("{} collector cluster is not defined on cluster manager level",
                                     config.getString("collector_cluster")));
  }
  cluster_ = cluster->info();

  if (cluster_->features() & Upstream::ClusterInfo::Features::HTTP2) {
    throw EnvoyException(
        fmt::format("Zipkin collector service (cluster {}) can be accessed over http1.1 only",
                    cluster_->name()));
  }

  std::string collector_endpoint = config.getString("collector_endpoint");

  tls_.set(tls_slot_, [this, collector_endpoint](Event::Dispatcher& dispatcher)
                          -> ThreadLocal::ThreadLocalObjectSharedPtr {
                            Zipkin::Tracer tracer(local_info_.clusterName(),
                                                  local_info_.address()->asString());
                            tracer.setReporter(ZipkinReporter::NewInstance(
                                std::ref(*this), std::ref(dispatcher), collector_endpoint));
                            return ThreadLocal::ThreadLocalObjectSharedPtr{
                                new TlsZipkinTracer(std::move(tracer), *this)};
                          });
}

SpanPtr ZipkinDriver::startSpan(Http::HeaderMap& request_headers, const std::string&,
                                SystemTime start_time) {
  // TODO: start_time is not really used.
  // Need to figure out whether or not it is really needed
  // A new timestamp is currently generated for a new span

  Zipkin::Tracer& tracer = tls_.getTyped<TlsZipkinTracer>(tls_slot_).tracer_;
  ZipkinSpanPtr active_span;
  Zipkin::Span new_zipkin_span;

  if (request_headers.OtSpanContext()) {
    // Get the open tracing span context.
    // This header contains B3 annotations set by the downstream caller.
    // The context built from this header allows the zipkin tracer to
    // properly set the span id and the parent span id.
    Zipkin::SpanContext context;

    context.populateFromString(request_headers.OtSpanContext()->value().c_str());
    new_zipkin_span = tracer.startSpan(request_headers.Host()->value().c_str(),
                                       start_time.time_since_epoch().count(), context);
  } else {
    new_zipkin_span = tracer.startSpan(request_headers.Host()->value().c_str(),
                                       start_time.time_since_epoch().count());
  }

  // Set the trace-id and span-id headers properly, based on the newly-created span structure
  request_headers.insertXB3TraceId().value(new_zipkin_span.traceIdAsHexString());
  request_headers.insertXB3SpanId().value(new_zipkin_span.idAsHexString());

  // Set the parent-span header properly, based on the newly-created span structure
  if (new_zipkin_span.isSet().parent_id) {
    request_headers.insertXB3ParentSpanId().value(new_zipkin_span.parentIdAsHexString());
  }

  // Set sampled header
  request_headers.insertXB3Sampled().value(std::string(("1")));

  Zipkin::SpanContext new_span_context(new_zipkin_span);

  // Set the ot-span-context with the new context
  request_headers.insertOtSpanContext().value(new_span_context.serializeToString());
  active_span.reset(new ZipkinSpan(new_zipkin_span));

  return std::move(active_span);
}

ZipkinReporter::ZipkinReporter(ZipkinDriver& driver, Event::Dispatcher& dispatcher,
                               const std::string& collector_endpoint)
    : driver_(driver), collector_endpoint_(collector_endpoint) {
  flush_timer_ = dispatcher.createTimer([this]() -> void {
    driver_.tracerStats().timer_flushed_.inc();
    flushSpans();
    enableTimer();
  });

  uint64_t min_flush_spans =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.min_flush_spans", 5U);
  span_buffer_.allocateBuffer(min_flush_spans);

  enableTimer();
}

std::unique_ptr<Zipkin::Reporter>
ZipkinReporter::NewInstance(ZipkinDriver& driver, Event::Dispatcher& dispatcher,
                            const std::string& collector_endpoint) {
  return std::unique_ptr<Zipkin::Reporter>(
      new ZipkinReporter(driver, dispatcher, collector_endpoint));
}

void ZipkinReporter::reportSpan(Zipkin::Span&& span) {
  span_buffer_.addSpan(std::move(span));

  uint64_t min_flush_spans =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.min_flush_spans", 5U);

  if (span_buffer_.pendingSpans() == min_flush_spans) {
    flushSpans();
  }
}

void ZipkinReporter::enableTimer() {
  uint64_t flush_interval =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.flush_interval_ms", 5000U);
  flush_timer_->enableTimer(std::chrono::milliseconds(flush_interval));
}

void ZipkinReporter::flushSpans() {
  if (span_buffer_.pendingSpans()) {
    driver_.tracerStats().spans_sent_.add(span_buffer_.pendingSpans());

    std::string request_body = span_buffer_.toStringifiedJsonArray();
    Http::MessagePtr message(new Http::RequestMessageImpl());
    message->headers().insertMethod().value(Http::Headers::get().MethodValues.Post);
    message->headers().insertPath().value(collector_endpoint_);
    message->headers().insertHost().value(driver_.cluster()->name());
    message->headers().insertContentType().value(std::string("application/json"));

    Buffer::InstancePtr body(new Buffer::OwnedImpl());
    body->add(request_body);
    message->body() = std::move(body);

    uint64_t timeout =
        driver_.runtime().snapshot().getInteger("tracing.zipkin.request_timeout", 5000U);
    driver_.clusterManager()
        .httpAsyncClientForCluster(driver_.cluster()->name())
        .send(std::move(message), *this, std::chrono::milliseconds(timeout));

    span_buffer_.flush();
  }
}

void ZipkinReporter::onFailure(Http::AsyncClient::FailureReason) {
  driver_.tracerStats().reports_dropped_.inc();
}

void ZipkinReporter::onSuccess(Http::MessagePtr&& http_response) {
  if (Http::Utility::getResponseStatus(http_response->headers()) !=
      enumToInt(Http::Code::Accepted)) {
    driver_.tracerStats().reports_sent_.inc();
  } else {
    driver_.tracerStats().reports_dropped_.inc();
  }
}

} // Tracing
