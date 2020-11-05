#pragma once

#include <memory>

#include "envoy/grpc/async_client.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/connection.h"
#include "envoy/service/metrics/v3/metrics_service.pb.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/sink.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"
#include "common/grpc/typed_async_client.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {

/**
 * Interface for metrics streamer.
 */
template <class ResponseProto>
class GrpcMetricsStreamer : public Grpc::AsyncStreamCallbacks<ResponseProto> {
public:
  ~GrpcMetricsStreamer() override = default;

  /**
   * Send Metrics Message.
   * @param message supplies the metrics to send.
   */
  virtual void
  send(Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily>& metrics) PURE;

  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
  void onReceiveMessage(std::unique_ptr<ResponseProto>&&) override {}
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override{};
};

template <class ResponseProto>
using GrpcMetricsStreamerSharedPtr = std::shared_ptr<GrpcMetricsStreamer<ResponseProto>>;

/**
 * Production implementation of GrpcMetricsStreamer
 */
class GrpcMetricsStreamerImpl
    : public Singleton::Instance,
      public GrpcMetricsStreamer<envoy::service::metrics::v3::StreamMetricsResponse> {
public:
  GrpcMetricsStreamerImpl(Grpc::AsyncClientFactoryPtr&& factory,
                          const LocalInfo::LocalInfo& local_info,
                          envoy::config::core::v3::ApiVersion transport_api_version);

  // GrpcMetricsStreamer
  void
  send(Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily>& metrics) override;

  // Grpc::AsyncStreamCallbacks
  void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override { stream_ = nullptr; }

private:
  Grpc::AsyncStream<envoy::service::metrics::v3::StreamMetricsMessage> stream_{};
  Grpc::AsyncClient<envoy::service::metrics::v3::StreamMetricsMessage,
                    envoy::service::metrics::v3::StreamMetricsResponse>
      client_;
  const LocalInfo::LocalInfo& local_info_;
  const Protobuf::MethodDescriptor& service_method_;
  const envoy::config::core::v3::ApiVersion transport_api_version_;
};

using GrpcMetricsStreamerImplPtr = std::unique_ptr<GrpcMetricsStreamerImpl>;

/**
 * Stat Sink that flushes metrics via a gRPC service.
 */
template <class ResponseProto> class MetricsServiceSink : public Stats::Sink {
public:
  // MetricsService::Sink
  MetricsServiceSink(const GrpcMetricsStreamerSharedPtr<ResponseProto>& grpc_metrics_streamer,
                     const bool report_counters_as_deltas)
      : grpc_metrics_streamer_(grpc_metrics_streamer),
        report_counters_as_deltas_(report_counters_as_deltas) {}
  void flush(Stats::MetricSnapshot& snapshot) override {
    metrics_.Clear();

    // TODO(mrice32): there's probably some more sophisticated preallocation we can do here where we
    // actually preallocate the submessages and then pass ownership to the proto (rather than just
    // preallocating the pointer array).
    metrics_.Reserve(snapshot.counters().size() + snapshot.gauges().size() +
                     snapshot.histograms().size());
    int64_t snapshot_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   snapshot.snapshotTime().time_since_epoch())
                                   .count();
    for (const auto& counter : snapshot.counters()) {
      if (counter.counter_.get().used()) {
        flushCounter(counter, snapshot_time_ms);
      }
    }

    for (const auto& gauge : snapshot.gauges()) {
      if (gauge.get().used()) {
        flushGauge(gauge.get(), snapshot_time_ms);
      }
    }

    for (const auto& histogram : snapshot.histograms()) {
      if (histogram.get().used()) {
        flushHistogram(histogram.get(), snapshot_time_ms);
      }
    }

    grpc_metrics_streamer_->send(metrics_);
  }
  void onHistogramComplete(const Stats::Histogram&, uint64_t) override {}

private:
  void flushCounter(const Stats::MetricSnapshot::CounterSnapshot& counter_snapshot,
                    int64_t snapshot_time_ms) {
    io::prometheus::client::MetricFamily* metrics_family = metrics_.Add();
    metrics_family->set_type(io::prometheus::client::MetricType::COUNTER);
    metrics_family->set_name(counter_snapshot.counter_.get().name());
    auto* metric = metrics_family->add_metric();
    metric->set_timestamp_ms(snapshot_time_ms);
    auto* counter_metric = metric->mutable_counter();
    if (report_counters_as_deltas_) {
      counter_metric->set_value(counter_snapshot.delta_);
    } else {
      counter_metric->set_value(counter_snapshot.counter_.get().value());
    }
  }

  void flushGauge(const Stats::Gauge& gauge, int64_t snapshot_time_ms) {
    io::prometheus::client::MetricFamily* metrics_family = metrics_.Add();
    metrics_family->set_type(io::prometheus::client::MetricType::GAUGE);
    metrics_family->set_name(gauge.name());
    auto* metric = metrics_family->add_metric();
    metric->set_timestamp_ms(snapshot_time_ms);
    auto* gauge_metric = metric->mutable_gauge();
    gauge_metric->set_value(gauge.value());
  }

  void flushHistogram(const Stats::ParentHistogram& envoy_histogram, int64_t snapshot_time_ms) {
    // TODO(ramaraochavali): Currently we are sending both quantile information and bucket
    // information. We should make this configurable if it turns out that sending both affects
    // performance.

    // Add summary information for histograms.
    io::prometheus::client::MetricFamily* summary_metrics_family = metrics_.Add();
    summary_metrics_family->set_type(io::prometheus::client::MetricType::SUMMARY);
    summary_metrics_family->set_name(envoy_histogram.name());
    auto* summary_metric = summary_metrics_family->add_metric();
    summary_metric->set_timestamp_ms(snapshot_time_ms);
    auto* summary = summary_metric->mutable_summary();
    const Stats::HistogramStatistics& hist_stats = envoy_histogram.intervalStatistics();
    for (size_t i = 0; i < hist_stats.supportedQuantiles().size(); i++) {
      auto* quantile = summary->add_quantile();
      quantile->set_quantile(hist_stats.supportedQuantiles()[i]);
      quantile->set_value(hist_stats.computedQuantiles()[i]);
    }

    // Add bucket information for histograms.
    io::prometheus::client::MetricFamily* histogram_metrics_family = metrics_.Add();
    histogram_metrics_family->set_type(io::prometheus::client::MetricType::HISTOGRAM);
    histogram_metrics_family->set_name(envoy_histogram.name());
    auto* histogram_metric = histogram_metrics_family->add_metric();
    histogram_metric->set_timestamp_ms(snapshot_time_ms);
    auto* histogram = histogram_metric->mutable_histogram();
    histogram->set_sample_count(hist_stats.sampleCount());
    histogram->set_sample_sum(hist_stats.sampleSum());
    for (size_t i = 0; i < hist_stats.supportedBuckets().size(); i++) {
      auto* bucket = histogram->add_bucket();
      bucket->set_upper_bound(hist_stats.supportedBuckets()[i]);
      bucket->set_cumulative_count(hist_stats.computedBuckets()[i]);
    }
  }

  GrpcMetricsStreamerSharedPtr<ResponseProto> grpc_metrics_streamer_;
  Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily> metrics_;
  const bool report_counters_as_deltas_;
};

} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
