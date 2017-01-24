#pragma once

#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"

#include "lightstep/tracer.h"

namespace Tracing {

#define LIGHTSTEP_TRACER_STATS(COUNTER)                                                            \
  COUNTER(spans_sent)                                                                              \
  COUNTER(timer_flushed)

struct LightstepTracerStats {
  LIGHTSTEP_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

enum class Reason {
  NotTraceableRequestId,
  HealthCheck,
  Sampling,
  ServiceForced,
  ClientForced,
};

struct Decision {
  Reason reason;
  bool is_tracing;
};

class HttpTracerUtility {
public:
  /**
   * Request might be traceable if x-request-id is traceable uuid or we do sampling tracing.
   * Note: there is a global switch which turns off tracing completely on server side.
   *
   * @return decision if request is traceable or not and Reason why.
   **/
  static Decision isTracing(const Http::AccessLog::RequestInfo& request_info,
                            const Http::HeaderMap& request_headers);

  /**
   * Mutate request headers if request needs to be traced.
   */
  static void mutateHeaders(Http::HeaderMap& request_headers, Runtime::Loader& runtime);

  /**
   * Fill in span tags based on request.
   */
  static void populateSpan(SpanPtr& active_span, const std::string& service_node,
                           const Http::HeaderMap& request_headers,
                           const Http::AccessLog::RequestInfo& request_info);

  /**
   * Fill in span tags based on the response headers.
   */
  static void finalizeSpan(SpanPtr& active_span, const Http::AccessLog::RequestInfo& request_info);
};

class HttpNullTracer : public HttpTracer {
public:
  // Tracing::HttpTracer
  void initializeDriver(DriverPtr&&) override {}
  SpanPtr startSpan(const Config&, const Http::HeaderMap&,
                    const Http::AccessLog::RequestInfo&) override {
    return nullptr;
  }
};

class HttpTracerImpl : public HttpTracer {
public:
  HttpTracerImpl(Runtime::Loader& runtime, const LocalInfo::LocalInfo& local_info,
                 Stats::Store& stats);

  // Tracing::HttpTracer
  void initializeDriver(DriverPtr&& driver) override;

  SpanPtr startSpan(const Config& config, const Http::HeaderMap& request_headers,
                    const Http::AccessLog::RequestInfo& request_info) override;

private:
  Runtime::Loader& runtime_;
  const LocalInfo::LocalInfo& local_info_;
  DriverPtr driver_;
};

class LightStepSpan : public Span {
public:
  LightStepSpan(lightstep::Span& span);

  void finishSpan() override;
  void setTag(const std::string& name, const std::string& value) override;

private:
  lightstep::Span span_;
};

/**
 * LightStep (http://lightstep.com/) provides tracing capabilities, aggregation, visualization of
 * application trace data.
 *
 * LightStepSink is for flushing data to LightStep collectors.
 */
class LightStepDriver : public Driver {
public:
  LightStepDriver(const Json::Object& config, Upstream::ClusterManager& cluster_manager,
                  Stats::Store& stats, ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                  std::unique_ptr<lightstep::TracerOptions> options);

  // Tracer::TracingDriver
  SpanPtr startSpan(const std::string& operation_name, SystemTime start_time) override;

  Upstream::ClusterManager& clusterManager() { return cm_; }
  Upstream::ClusterInfoPtr cluster() { return cluster_; }
  Runtime::Loader& runtime() { return runtime_; }
  LightstepTracerStats& tracerStats() { return tracer_stats_; }

private:
  struct TlsLightStepTracer : ThreadLocal::ThreadLocalObject {
    TlsLightStepTracer(lightstep::Tracer tracer, LightStepDriver& driver);

    void shutdown() override {}

    lightstep::Tracer tracer_;
    LightStepDriver& driver_;
  };

  Upstream::ClusterManager& cm_;
  Upstream::ClusterInfoPtr cluster_;
  LightstepTracerStats tracer_stats_;
  ThreadLocal::Instance& tls_;
  Runtime::Loader& runtime_;
  std::unique_ptr<lightstep::TracerOptions> options_;
  uint32_t tls_slot_;
};

class LightStepRecorder : public lightstep::Recorder, Http::AsyncClient::Callbacks {
public:
  LightStepRecorder(const lightstep::TracerImpl& tracer, LightStepDriver& driver,
                    Event::Dispatcher& dispatcher);

  // lightstep::Recorder
  void RecordSpan(lightstep::collector::Span&& span) override;
  bool FlushWithTimeout(lightstep::Duration) override;

  // Http::AsyncClient::Callbacks
  void onSuccess(Http::MessagePtr&&) override;
  void onFailure(Http::AsyncClient::FailureReason) override;

  static std::unique_ptr<lightstep::Recorder> NewInstance(LightStepDriver& driver,
                                                          Event::Dispatcher& dispatcher,
                                                          const lightstep::TracerImpl& tracer);

private:
  void enableTimer();
  void flushSpans();

  lightstep::ReportBuilder builder_;
  LightStepDriver& driver_;
  Event::TimerPtr flush_timer_;
};

} // Tracing
