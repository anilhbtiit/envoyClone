#pragma once

#include "envoy/http/filter.h"

namespace Grpc {

/**
 * See docs/configuration/http_filters/grpc_http1_bridge_filter.rst
 */
class Http1BridgeFilter : public Http::StreamFilter {
public:
  Http1BridgeFilter(Stats::Store& stats_store) : stats_store_(stats_store) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool bool_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap& trailers) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks&) override {}

private:
  void chargeStat(const Http::HeaderMap& headers);
  void setupStatTracking(const Http::HeaderMap& headers);

  Stats::Store& stats_store_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::HeaderMap* response_headers_{};
  bool do_bridging_{};
  bool do_stat_tracking_{};
  std::string cluster_;
  std::string grpc_service_;
  std::string grpc_method_;
};

} // Grpc
