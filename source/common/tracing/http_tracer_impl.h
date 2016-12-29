#pragma once

#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"

#include "lightstep/tracer.h"

namespace Tracing {

#define HTTP_TRACER_STATS(COUNTER)                                                                 \
  COUNTER(global_switch_off)                                                                       \
  COUNTER(invalid_request_id)                                                                      \
  COUNTER(random_sampling)                                                                         \
  COUNTER(service_forced)                                                                          \
  COUNTER(client_enabled)                                                                          \
  COUNTER(doing_tracing)                                                                           \
  COUNTER(flush)                                                                                   \
  COUNTER(not_traceable)                                                                           \
  COUNTER(health_check)                                                                            \
  COUNTER(traceable)

struct HttpTracerStats {
  HTTP_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

#define LIGHTSTEP_TRACER_STATS(COUNTER)                                                            \
  COUNTER(spans_sent)                                                                              \
  COUNTER(timer_flushed)

struct LightstepTracerStats {
  LIGHTSTEP_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

class TracingContextImpl : public TracingContext {
public:
  TracingContextImpl(const std::string& service_node, HttpTracer& http_tracer,
                     const TracingConfig& config);

  void startSpan(const Http::AccessLog::RequestInfo& request_info,
                 const Http::HeaderMap& request_headers) override;

  void finishSpan(const Http::AccessLog::RequestInfo& request_info,
                  const Http::HeaderMap* response_headers) override;

private:
  const std::string service_node_;
  HttpTracer& http_tracer_;
  const TracingConfig& tracing_config_;
  SpanPtr active_span_;
};

class LightStepSpan : public Span {
public:
  LightStepSpan(lightstep::Span& span);

  void finishSpan() override;
  void setTag(const std::string& name, const std::string& value) override;

private:
  lightstep::Span span_;
};

class HttpNullTracer : public HttpTracer {
public:
  // Tracing::HttpTracer
  void initializeDriver(TracingDriverPtr&&) override {}
  SpanPtr startSpan(const std::string&, SystemTime) override { return nullptr; }
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
};

class HttpTracerImpl : public HttpTracer {
public:
  HttpTracerImpl(Runtime::Loader& runtime, Stats::Store& stats);

  // Tracing::HttpTracer
  void initializeDriver(TracingDriverPtr&& driver) override;

  SpanPtr startSpan(const std::string& operation_name, SystemTime start_time) override;

private:
  void populateStats(const Decision& decision);

  Runtime::Loader& runtime_;
  HttpTracerStats stats_;
  TracingDriverPtr driver_;
};

/**
 * LightStep (http://lightstep.com/) provides tracing capabilities, aggregation, visualization of
 * application trace data.
 *
 * LightStepSink is for flushing data to LightStep collectors.
 */
class LightStepDriver : public TracingDriver {
public:
  LightStepDriver(const Json::Object& config, Upstream::ClusterManager& cluster_manager,
                  Stats::Store& stats, const std::string& service_node, ThreadLocal::Instance& tls,
                  Runtime::Loader& runtime, std::unique_ptr<lightstep::TracerOptions> options);

  // Tracer::TracingDriver
  SpanPtr startSpan(const std::string& operation_name, SystemTime start_time) override;

  Upstream::ClusterManager& clusterManager() { return cm_; }
  const std::string& collectorCluster() { return collector_cluster_; }
  Runtime::Loader& runtime() { return runtime_; }
  Stats::Store& statsStore() { return stats_store_; }
  LightstepTracerStats& tracerStats() { return tracer_stats_; }

private:
  struct TlsLightStepTracer : ThreadLocal::ThreadLocalObject {
    TlsLightStepTracer(lightstep::Tracer tracer, LightStepDriver& driver);

    void shutdown() override {}

    lightstep::Tracer tracer_;
    LightStepDriver& driver_;
  };

  const std::string collector_cluster_;
  Upstream::ClusterManager& cm_;
  Stats::Store& stats_store_;
  LightstepTracerStats tracer_stats_;
  const std::string service_node_;
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
