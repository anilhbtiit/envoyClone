#pragma once

#include "envoy/http/filter.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Http {
namespace Matching {

/**
 * Implementation of HttpMatchingData, providing HTTP specific data to
 * the match tree.
 */
class HttpMatchingDataImpl : public HttpMatchingData {
public:
  explicit HttpMatchingDataImpl(const Network::ConnectionInfoProvider& connection_info_provider)
      : connection_info_provider_(connection_info_provider) {}

  static absl::string_view name() { return "http"; }

  void onRequestHeaders(const RequestHeaderMap& request_headers) {
    request_headers_ = &request_headers;
  }

  void onRequestTrailers(const RequestTrailerMap& request_trailers) {
    request_trailers_ = &request_trailers;
  }

  void onResponseHeaders(const ResponseHeaderMap& response_headers) {
    response_headers_ = &response_headers;
  }

  void onResponseTrailers(const ResponseTrailerMap& response_trailers) {
    response_trailers_ = &response_trailers;
  }

  RequestHeaderMapOptConstRef requestHeaders() const override {
    return makeOptRefFromPtr(request_headers_);
  }

  RequestTrailerMapOptConstRef requestTrailers() const override {
    return makeOptRefFromPtr(request_trailers_);
  }

  ResponseHeaderMapOptConstRef responseHeaders() const override {
    return makeOptRefFromPtr(response_headers_);
  }

  ResponseTrailerMapOptConstRef responseTrailers() const override {
    return makeOptRefFromPtr(response_trailers_);
  }

  const Network::ConnectionInfoProvider& connectionInfoProvider() const override {
    return connection_info_provider_;
  }

private:
  const Network::ConnectionInfoProvider& connection_info_provider_;
  const RequestHeaderMap* request_headers_{};
  const ResponseHeaderMap* response_headers_{};
  const RequestTrailerMap* request_trailers_{};
  const ResponseTrailerMap* response_trailers_{};
};

using HttpMatchingDataImplSharedPtr = std::shared_ptr<HttpMatchingDataImpl>;

struct HttpFilterActionContext {
  const std::string& stat_prefix_;
  Server::Configuration::ServerFactoryContext& factory_context_;
};
} // namespace Matching
} // namespace Http
} // namespace Envoy
