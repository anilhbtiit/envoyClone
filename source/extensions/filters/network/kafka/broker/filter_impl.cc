#include "extensions/filters/network/kafka/broker/filter_impl.h"

#include "extensions/filters/network/kafka/broker/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

void Forwarder::onMessage(AbstractRequestSharedPtr request) {
  const RequestHeader& header = request->request_header_;
  response_decoder_.expectResponse(header.api_key_, header.api_version_);
}

void Forwarder::onFailedParse(RequestParseFailureSharedPtr parse_failure) {
  const RequestHeader& header = parse_failure->request_header_;
  response_decoder_.expectResponse(header.api_key_, header.api_version_);
}

MetricTrackingCallback::MetricTrackingCallback(Stats::Scope& scope, TimeSource& time_source,
                                               const std::string& stat_prefix)
    : MetricTrackingCallback{time_source,
                             std::make_shared<RichRequestMetricsImpl>(scope, stat_prefix),
                             std::make_shared<RichResponseMetricsImpl>(scope, stat_prefix)} {};

MetricTrackingCallback::MetricTrackingCallback(TimeSource& time_source,
                                               RichRequestMetricsSharedPtr request_metrics,
                                               RichResponseMetricsSharedPtr response_metrics)
    : time_source_{time_source}, request_metrics_{request_metrics}, response_metrics_{
                                                                        response_metrics} {};

void MetricTrackingCallback::onMessage(AbstractRequestSharedPtr request) {
  const RequestHeader& header = request->request_header_;
  request_metrics_->onRequest(header.api_key_);

  const MonotonicTime request_arrival_ts = time_source_.monotonicTime();
  request_arrivals_[header.correlation_id_] = request_arrival_ts;
}

void MetricTrackingCallback::onMessage(AbstractResponseSharedPtr response) {
  const ResponseMetadata& metadata = response->metadata_;

  const MonotonicTime response_arrival_ts = time_source_.monotonicTime();
  const MonotonicTime request_arrival_ts = request_arrivals_[metadata.correlation_id_];
  request_arrivals_.erase(metadata.correlation_id_);

  const MonotonicTime::duration time_in_broker = response_arrival_ts - request_arrival_ts;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_in_broker);

  response_metrics_->onResponse(metadata.api_key_, ms.count());
}

void MetricTrackingCallback::onFailedParse(RequestParseFailureSharedPtr) {
  request_metrics_->onUnknownRequest();
}

void MetricTrackingCallback::onFailedParse(ResponseMetadataSharedPtr) {
  response_metrics_->onUnknownResponse();
}

std::map<int32_t, MonotonicTime>& MetricTrackingCallback::getRequestArrivalsForTest() {
  return request_arrivals_;
}

KafkaBrokerFilter::KafkaBrokerFilter(Stats::Scope& scope, TimeSource& time_source,
                                     const std::string& stat_prefix)
    : KafkaBrokerFilter{
          std::make_shared<MetricTrackingCallback>(scope, time_source, stat_prefix)} {};

KafkaBrokerFilter::KafkaBrokerFilter(const KafkaCallbackSharedPtr& metrics_callback)
    : response_decoder_{new ResponseDecoder({metrics_callback})},
      request_decoder_{new RequestDecoder(
          {std::make_shared<Forwarder>(*response_decoder_), metrics_callback})} {};

KafkaBrokerFilter::KafkaBrokerFilter(ResponseDecoderSharedPtr response_decoder,
                                     RequestDecoderSharedPtr request_decoder)
    : response_decoder_{response_decoder}, request_decoder_{request_decoder} {};

Network::FilterStatus KafkaBrokerFilter::onNewConnection() {
  return Network::FilterStatus::Continue;
}

void KafkaBrokerFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks&) {}

Network::FilterStatus KafkaBrokerFilter::onData(Buffer::Instance& data, bool) {
  ENVOY_LOG(trace, "data from Kafka client [{} request bytes]", data.length());
  try {
    request_decoder_->onData(data);
    return Network::FilterStatus::Continue;
  } catch (const EnvoyException& e) {
    ENVOY_LOG(info, "could not process data from Kafka client: {}", e.what());
    return Network::FilterStatus::StopIteration;
  }
}

Network::FilterStatus KafkaBrokerFilter::onWrite(Buffer::Instance& data, bool) {
  ENVOY_LOG(trace, "data from Kafka broker [{} response bytes]", data.length());
  try {
    response_decoder_->onData(data);
    return Network::FilterStatus::Continue;
  } catch (const EnvoyException& e) {
    ENVOY_LOG(info, "could not process data from Kafka broker: {}", e.what());
    return Network::FilterStatus::StopIteration;
  }
}

ResponseDecoderSharedPtr KafkaBrokerFilter::getResponseDecoderForTest() {
  return response_decoder_;
}

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
