#include "extensions/filters/http/ext_authz/ext_authz.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/router/config_impl.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

namespace {
// This in conjunction with filter decoder header and end_stream parameter to make sure that request
// has no massage body. In theory, end of stream should be received in most of cases which
// header-only request methods are used. However, this is not true when WebSocket handshake is
// performed which the request method is a GET, but the end of stream is false.
bool isHeaderOnlyMethod(absl::string_view method) {
  // List of request headers-only method (https://tools.ietf.org/html/rfc7231#section-4.3).
  const static std::vector<std::string>* keys = new std::vector<std::string>(
      {Http::Headers::get().MethodValues.Get, Http::Headers::get().MethodValues.Head,
       Http::Headers::get().MethodValues.Connect, Http::Headers::get().MethodValues.Options,
       Http::Headers::get().MethodValues.Trace});

  return std::any_of(keys->begin(), keys->end(), [&method](const auto& key) { return key == method; });
}
} // namespace

void FilterConfigPerRoute::merge(const FilterConfigPerRoute& other) {
  disabled_ = other.disabled_;
  auto begin_it = other.context_extensions_.begin();
  auto end_it = other.context_extensions_.end();
  for (auto it = begin_it; it != end_it; ++it) {
    context_extensions_[it->first] = it->second;
  }
}

void Filter::initiateCall(const Http::HeaderMap& headers) {
  Router::RouteConstSharedPtr route = callbacks_->route();
  if (route == nullptr || route->routeEntry() == nullptr) {
    return;
  }
  cluster_ = callbacks_->clusterInfo();
  if (!cluster_) {
    return;
  }

  // Fast route - if we are disabled, no need to merge.
  const FilterConfigPerRoute* specific_per_route_config =
      Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfigPerRoute>(
          HttpFilterNames::get().ExtAuthorization, route);
  if (specific_per_route_config != nullptr) {
    if (specific_per_route_config->disabled()) {
      return;
    }
  }

  // We are not disabled - get a merged view of the config:
  auto&& maybe_merged_per_route_config =
      Http::Utility::getMergedPerFilterConfig<FilterConfigPerRoute>(
          HttpFilterNames::get().ExtAuthorization, route,
          [](FilterConfigPerRoute& cfg_base, const FilterConfigPerRoute& cfg) {
            cfg_base.merge(cfg);
          });

  Protobuf::Map<ProtobufTypes::String, ProtobufTypes::String> context_extensions;
  if (maybe_merged_per_route_config) {
    context_extensions = maybe_merged_per_route_config.value().takeContextExtensions();
  }
  Filters::Common::ExtAuthz::CheckRequestUtils::createHttpCheck(
      callbacks_, headers, std::move(context_extensions), check_request_);

  state_ = State::Calling;
  // Don't let the filter chain continue as we are going to invoke check call.
  filter_return_ = FilterReturn::StopDecoding;
  initiating_call_ = true;
  ENVOY_STREAM_LOG(trace, "ext_authz filter calling authorization server", *callbacks_);
  client_->check(*this, check_request_, callbacks_->activeSpan());
  initiating_call_ = false;
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  request_headers_ = &headers;

  buffer_data_ = config_->withRequestData() &&
                 !(end_stream || isHeaderOnlyMethod(headers.Method()->value().getStringView()));
  if (buffer_data_) {
    ENVOY_STREAM_LOG(debug, "ext_authz is buffering", *callbacks_);

    if (!config_->allowPartialMessage()) {
      callbacks_->setDecoderBufferLimit(config_->maxRequestBytes());
    }

    return Http::FilterHeadersStatus::StopIteration;
  }

  initiateCall(headers);
  return filter_return_ == FilterReturn::StopDecoding ? Http::FilterHeadersStatus::StopIteration
                                                      : Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance&, bool end_stream) {

   if (buffer_data_) {
    if (!end_stream) {
      if (config_->allowPartialMessage()) {
        const auto* buffer = callbacks_->decodingBuffer();
        if (buffer != nullptr && buffer->length() < config_->maxRequestBytes()) {
          return Http::FilterDataStatus::StopIterationAndBuffer;  
        }
      } else {
        return Http::FilterDataStatus::StopIterationAndBuffer;  
      }
    }
  
    ENVOY_STREAM_LOG(debug, "ext_authz initiating call after buffering", *callbacks_);
    initiateCall(*request_headers_);  
  }

  return filter_return_ == FilterReturn::StopDecoding
             ? Http::FilterDataStatus::StopIterationAndWatermark
             : Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::HeaderMap&) {
  return filter_return_ == FilterReturn::StopDecoding ? Http::FilterTrailersStatus::StopIteration
                                                      : Http::FilterTrailersStatus::Continue;
}

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

void Filter::onDestroy() {
  if (state_ == State::Calling) {
    state_ = State::Complete;
    client_->cancel();
  }
}

void Filter::onComplete(Filters::Common::ExtAuthz::ResponsePtr&& response) {
  ASSERT(cluster_);
  state_ = State::Complete;
  using Filters::Common::ExtAuthz::CheckStatus;

  switch (response->status) {
  case CheckStatus::OK:
    cluster_->statsScope().counter("ext_authz.ok").inc();
    break;
  case CheckStatus::Error:
    cluster_->statsScope().counter("ext_authz.error").inc();
    break;
  case CheckStatus::Denied:
    cluster_->statsScope().counter("ext_authz.denied").inc();
    Http::CodeStats::ResponseStatInfo info{config_->scope(),
                                           cluster_->statsScope(),
                                           EMPTY_STRING,
                                           enumToInt(response->status_code),
                                           true,
                                           EMPTY_STRING,
                                           EMPTY_STRING,
                                           EMPTY_STRING,
                                           EMPTY_STRING,
                                           false};
    config_->httpContext().codeStats().chargeResponseStat(info);
    break;
  }

  ENVOY_STREAM_LOG(trace, "ext_authz received status code {}", *callbacks_,
                   enumToInt(response->status_code));

  // We fail open/fail close based of filter config
  // if there is an error contacting the service.
  if (response->status == CheckStatus::Denied ||
      (response->status == CheckStatus::Error && !config_->failureModeAllow())) {
    ENVOY_STREAM_LOG(debug, "ext_authz rejected the request", *callbacks_);
    ENVOY_STREAM_LOG(trace, "ext_authz downstream header(s):", *callbacks_);
    callbacks_->sendLocalReply(response->status_code, response->body,
                               [& headers = response->headers_to_add, &callbacks = *callbacks_](
                                   Http::HeaderMap& response_headers) -> void {
                                 for (const auto& header : headers) {
                                   response_headers.remove(header.first);
                                   response_headers.addCopy(header.first, header.second);
                                   ENVOY_STREAM_LOG(trace, " '{}':'{}'", callbacks,
                                                    header.first.get(), header.second);
                                 }
                               },
                               absl::nullopt);
    callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::UnauthorizedExternalService);
  } else {
    ENVOY_STREAM_LOG(debug, "ext_authz accepted the request", *callbacks_);
    // Let the filter chain continue.
    filter_return_ = FilterReturn::ContinueDecoding;
    if (config_->failureModeAllow() && response->status == CheckStatus::Error) {
      // Status is Error and yet we are allowing the request. Click a counter.
      cluster_->statsScope().counter("ext_authz.failure_mode_allowed").inc();
    }
    // Only send headers if the response is ok.
    if (response->status == CheckStatus::OK) {
      ENVOY_STREAM_LOG(trace, "ext_authz upstream header(s):", *callbacks_);
      for (const auto& header : response->headers_to_add) {
        Http::HeaderEntry* header_to_modify = request_headers_->get(header.first);
        if (header_to_modify) {
          header_to_modify->value(header.second.c_str(), header.second.size());
        } else {
          request_headers_->addCopy(header.first, header.second);
        }
        ENVOY_STREAM_LOG(trace, " '{}':'{}'", *callbacks_, header.first.get(), header.second);
      }
      for (const auto& header : response->headers_to_append) {
        Http::HeaderEntry* header_to_modify = request_headers_->get(header.first);
        if (header_to_modify) {
          Http::HeaderMapImpl::appendToHeader(header_to_modify->value(), header.second);
          ENVOY_STREAM_LOG(trace, " '{}':'{}'", *callbacks_, header.first.get(), header.second);
        }
      }
    }

    if (!initiating_call_) {
      // We got completion async. Let the filter chain continue.
      callbacks_->continueDecoding();
    }
  }
}

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
