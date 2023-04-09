#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "source/common/tracing/null_span_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

OpenTelemetryGrpcMetricsExporterImpl::OpenTelemetryGrpcMetricsExporterImpl(
    Grpc::RawAsyncClientSharedPtr raw_async_client)
    : OpenTelemetryGrpcMetricsExporter(raw_async_client),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "opentelemetry.proto.collector.metrics.v1.MetricsService.Export")) {}

void OpenTelemetryGrpcMetricsExporterImpl::send(MetricsExportRequestPtr&& export_request) {
  if (export_request == nullptr) {
    return;
  }

  client_->send(service_method_, *export_request, *this, Tracing::NullSpan::instance(),
                Http::AsyncClient::RequestOptions());
}

void OpenTelemetryGrpcMetricsExporterImpl::onSuccess(
    Grpc::ResponsePtr<MetricsExportResponse>&& export_response, Tracing::Span&) {
  if (export_response->has_partial_success()) {
    ENVOY_LOG(warn,
              "export response with partial success; {} rejected, collector message: {}",
              export_response->partial_success().rejected_data_points(),
              export_response->partial_success().error_message());
  }
}

void OpenTelemetryGrpcMetricsExporterImpl::onFailure(Grpc::Status::GrpcStatus response_status,
    const std::string& response_message, Tracing::Span&) {
    ENVOY_LOG(warn,
              "export failure; status: {}, message: {}", response_status, response_message);
}

MetricsExportRequestPtr MetricsFlusher::flush(Stats::MetricSnapshot& snapshot) const {
  auto request = std::make_unique<MetricsExportRequest>();
  auto* resource_metrics = request->add_resource_metrics();
  auto* scope_metrics = resource_metrics->add_scope_metrics();

  int64_t snapshot_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 snapshot.snapshotTime().time_since_epoch())
                                 .count();

  for (const auto& gauge : snapshot.gauges()) {
    if (predicate_(gauge)) {
      flushGauge(*scope_metrics->add_metrics(), gauge, snapshot_time_ns);
    }
  }

  for (const auto& counter : snapshot.counters()) {
    if (predicate_(counter.counter_)) {
      flushCounter(*scope_metrics->add_metrics(), counter, snapshot_time_ns);
    }
  }

  for (const auto& histogram : snapshot.histograms()) {
    if (predicate_(histogram)) {
      flushHistogram(*scope_metrics->add_metrics(), histogram, snapshot_time_ns);
    }
  }

  return request;
}

void MetricsFlusher::flushGauge(opentelemetry::proto::metrics::v1::Metric& metric,
                                const Stats::Gauge& gauge_stat, int64_t snapshot_time_ns) const {
  auto* data_point = metric.mutable_gauge()->add_data_points();
  data_point->set_time_unix_nano(snapshot_time_ns);
  setMetricCommon(metric, *data_point, snapshot_time_ns, gauge_stat);

  data_point->set_as_int(gauge_stat.value());
}

void MetricsFlusher::flushCounter(opentelemetry::proto::metrics::v1::Metric& metric,
                                  const Stats::MetricSnapshot::CounterSnapshot& counter_snapshot,
                                  int64_t snapshot_time_ns) const {
  auto* sum = metric.mutable_sum();
  sum->set_is_monotonic(true);
  auto* data_point = sum->add_data_points();
  setMetricCommon(metric, *data_point, snapshot_time_ns, counter_snapshot.counter_);

  if (report_counters_as_deltas_) {
    sum->set_aggregation_temporality(
        opentelemetry::proto::metrics::v1::AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA);

    data_point->set_as_int(counter_snapshot.delta_);
  } else {
    sum->set_aggregation_temporality(
        opentelemetry::proto::metrics::v1::AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE);

    data_point->set_as_int(counter_snapshot.counter_.get().value());
  }
}

void MetricsFlusher::flushHistogram(opentelemetry::proto::metrics::v1::Metric& metric,
                                    const Stats::ParentHistogram& parent_histogram,
                                    int64_t snapshot_time_ns) const {
  auto* histogram = metric.mutable_histogram();
  auto* data_point = histogram->add_data_points();
  setMetricCommon(metric, *data_point, snapshot_time_ns, parent_histogram);

  histogram->set_aggregation_temporality(report_histograms_as_deltas_ ?
      opentelemetry::proto::metrics::v1::AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA
      : opentelemetry::proto::metrics::v1::AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE);

  const Stats::HistogramStatistics& histogram_stats = report_histograms_as_deltas_ ?
      parent_histogram.intervalStatistics() : parent_histogram.cumulativeStatistics();

  data_point->set_count(histogram_stats.sampleCount());
  data_point->set_sum(histogram_stats.sampleSum());
  // TODO(ohadvano): Support HistogramDataPoint's 'min' and 'max' values

  for (size_t i = 0; i < histogram_stats.supportedBuckets().size(); i++) {
    data_point->add_explicit_bounds(histogram_stats.supportedBuckets()[i]);
    data_point->add_bucket_counts(histogram_stats.computedBuckets()[i]);
  }
}

void MetricsFlusher::setMetricCommon(opentelemetry::proto::metrics::v1::Metric& metric,
                                     opentelemetry::proto::metrics::v1::NumberDataPoint& data_point,
                                     int64_t snapshot_time_ns, const Stats::Metric& stat) const {
  // TODO(ohadvano): Support start_time_unix_nano (optional field)
  data_point.set_time_unix_nano(snapshot_time_ns);

  if (!emit_labels_) {
    metric.set_name(stat.name());
    return;
  }

  metric.set_name(stat.tagExtractedName());
  for (const auto& tag : stat.tags()) {
    auto* attribute = data_point.add_attributes();
    attribute->set_key(tag.name_);
    attribute->mutable_value()->set_string_value(tag.value_);
  }
}

void MetricsFlusher::setMetricCommon(opentelemetry::proto::metrics::v1::Metric& metric,
                                     opentelemetry::proto::metrics::v1::HistogramDataPoint& data_point,
                                     int64_t snapshot_time_ns, const Stats::Metric& stat) const {
  // TODO(ohadvano): Support start_time_unix_nano (optional field)
  data_point.set_time_unix_nano(snapshot_time_ns);

  if (!emit_labels_) {
    metric.set_name(stat.name());
    return;
  }

  metric.set_name(stat.tagExtractedName());
  for (const auto& tag : stat.tags()) {
    auto* attribute = data_point.add_attributes();
    attribute->set_key(tag.name_);
    attribute->mutable_value()->set_string_value(tag.value_);
  }
}

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
