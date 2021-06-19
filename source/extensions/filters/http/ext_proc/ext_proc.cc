#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "source/extensions/filters/http/ext_proc/mutation_utils.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode;

using envoy::service::ext_proc::v3alpha::ImmediateResponse;
using envoy::service::ext_proc::v3alpha::ProcessingRequest;
using envoy::service::ext_proc::v3alpha::ProcessingResponse;

using Http::FilterDataStatus;
using Http::FilterHeadersStatus;
using Http::FilterTrailersStatus;
using Http::RequestHeaderMap;
using Http::RequestTrailerMap;
using Http::ResponseHeaderMap;
using Http::ResponseTrailerMap;

static const std::string kErrorPrefix = "ext_proc error";

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  Http::PassThroughFilter::setDecoderFilterCallbacks(callbacks);
  decoding_state_.setDecoderFilterCallbacks(callbacks);
}

void Filter::setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) {
  Http::PassThroughFilter::setEncoderFilterCallbacks(callbacks);
  encoding_state_.setEncoderFilterCallbacks(callbacks);
}

Filter::StreamOpenState Filter::openStream() {
  ENVOY_BUG(!processing_complete_, "openStream should not have been called");
  if (!stream_) {
    ENVOY_LOG(debug, "Opening gRPC stream to external processor");
    stream_ = client_->start(*this);
    stats_.streams_started_.inc();
    if (processing_complete_) {
      // Stream failed while starting and either onGrpcError or onGrpcClose was already called
      return sent_immediate_response_ ? StreamOpenState::Error : StreamOpenState::IgnoreError;
    }
  }
  return StreamOpenState::Ok;
}

void Filter::onDestroy() {
  // Make doubly-sure we no longer use the stream, as
  // per the filter contract.
  processing_complete_ = true;
  if (stream_) {
    if (stream_->close()) {
      stats_.streams_closed_.inc();
    }
  }
}

FilterHeadersStatus Filter::onHeaders(ProcessorState& state,
                                      Http::RequestOrResponseHeaderMap& headers, bool end_stream) {
  switch (openStream()) {
  case StreamOpenState::Error:
    return FilterHeadersStatus::StopIteration;
  case StreamOpenState::IgnoreError:
    return FilterHeadersStatus::Continue;
  case StreamOpenState::Ok:
    // Fall through
    break;
  }

  state.setHeaders(&headers);
  ProcessingRequest req;
  auto* headers_req = state.mutableHeaders(req);
  MutationUtils::headersToProto(headers, *headers_req->mutable_headers());
  headers_req->set_end_of_stream(end_stream);
  state.setCallbackState(ProcessorState::CallbackState::HeadersCallback);
  state.startMessageTimer(std::bind(&Filter::onMessageTimeout, this), config_->messageTimeout());
  ENVOY_LOG(debug, "Sending headers message");
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
  return FilterHeadersStatus::StopIteration;
}

FilterHeadersStatus Filter::decodeHeaders(RequestHeaderMap& headers, bool end_stream) {
  ENVOY_LOG(trace, "decodeHeaders: end_stream = {}", end_stream);
  if (end_stream) {
    decoding_state_.setCompleteBodyAvailable(true);
  }

  if (!decoding_state_.sendHeaders()) {
    ENVOY_LOG(trace, "decodeHeaders: Skipped");
    return FilterHeadersStatus::Continue;
  }

  const auto status = onHeaders(decoding_state_, headers, end_stream);
  ENVOY_LOG(trace, "decodeHeaders returning {}", status);
  return status;
}

FilterDataStatus Filter::onData(ProcessorState& state, Buffer::Instance& data, bool end_stream) {
  if (end_stream) {
    state.setCompleteBodyAvailable(true);
  }

  if (state.bodyReplaced()) {
    ENVOY_LOG(trace, "Clearing body chunk because CONTINUE_AND_REPLACE was returned");
    data.drain(data.length());
    return FilterDataStatus::Continue;
  }

  if (processing_complete_) {
    ENVOY_LOG(trace, "Continuing (processing complete)");
    return FilterDataStatus::Continue;
  }

  bool just_added_trailers = false;
  Http::HeaderMap* new_trailers = nullptr;
  if (end_stream && state.sendTrailers()) {
    // We're at the end of the stream, but the filter wants to process trailers.
    // According to the filter contract, this is the only place where we can
    // add trailers, even if we will return right after this and process them
    // later.
    ENVOY_LOG(trace, "Creating new, empty trailers");
    new_trailers = state.addTrailers();
    state.setTrailersAvailable(true);
    just_added_trailers = true;
  }

  if (state.callbackState() == ProcessorState::CallbackState::HeadersCallback) {
    ENVOY_LOG(trace, "Header processing still in progress -- holding body data");
    // We don't know what to do with the body until the response comes back.
    // We must buffer it in case we need it when that happens.
    if (end_stream) {
      return FilterDataStatus::StopIterationAndBuffer;
    } else {
      // Raise a watermark to prevent a buffer overflow until the response comes back.
      state.requestWatermark();
      return FilterDataStatus::StopIterationAndWatermark;
    }
  }

  FilterDataStatus result;
  switch (state.bodyMode()) {
  case ProcessingMode::BUFFERED:
    if (end_stream) {
      switch (openStream()) {
      case StreamOpenState::Error:
        return FilterDataStatus::StopIterationNoBuffer;
      case StreamOpenState::IgnoreError:
        return FilterDataStatus::Continue;
      case StreamOpenState::Ok:
        // Fall through
        break;
      }

      // The body has been buffered and we need to send the buffer
      ENVOY_LOG(debug, "Sending request body message");
      state.addBufferedData(data);
      sendBodyChunk(state, *state.bufferedData(),
                    ProcessorState::CallbackState::BufferedBodyCallback, true);
      // Since we just just moved the data into the buffer, return NoBuffer
      // so that we do not buffer this chunk twice.
      result = FilterDataStatus::StopIterationNoBuffer;
      break;
    }

    ENVOY_LOG(trace, "onData: Buffering");
    result = FilterDataStatus::StopIterationAndBuffer;
    break;

  case ProcessingMode::STREAMED: {
    switch (openStream()) {
    case StreamOpenState::Error:
      return FilterDataStatus::StopIterationNoBuffer;
    case StreamOpenState::IgnoreError:
      return FilterDataStatus::Continue;
    case StreamOpenState::Ok:
      // Fall through
      break;
    }

    auto next_chunk = std::make_unique<QueuedChunk>();
    // Clear the current chunk and save it on the queue while it's processed
    next_chunk->data.move(data);
    next_chunk->end_stream = end_stream;
    // Send the chunk, and ensure that we have watermarked so that we don't overflow
    // memory while waiting for responses.
    state.requestWatermark();
    sendBodyChunk(state, next_chunk->data, ProcessorState::CallbackState::StreamedBodyCallback,
                  end_stream);
    state.enqueueStreamingChunk(std::move(next_chunk));

    // At this point we will continue, but with no data, because that will come later
    if (end_stream) {
      // But we need to buffer the last chunk because it's our last chance to do stuff
      result = FilterDataStatus::StopIterationNoBuffer;
    } else {
      result = FilterDataStatus::Continue;
    }
    break;
  }

  case ProcessingMode::BUFFERED_PARTIAL:
    ENVOY_LOG(debug, "Ignoring unimplemented request body processing mode");
    result = FilterDataStatus::Continue;
    break;

  case ProcessingMode::NONE:
  default:
    result = FilterDataStatus::Continue;
    break;
  }

  if (just_added_trailers) {
    // If we get here, then we need to send the trailers message now
    switch (openStream()) {
    case StreamOpenState::Error:
      return FilterDataStatus::StopIterationNoBuffer;
    case StreamOpenState::IgnoreError:
      return FilterDataStatus::Continue;
    case StreamOpenState::Ok:
      // Fall through
      break;
    }

    sendTrailers(state, *new_trailers);
    return FilterDataStatus::StopIterationAndBuffer;
  }
  return result;
}

FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "decodeData({}): end_stream = {}", data.length(), end_stream);
  const auto status = onData(decoding_state_, data, end_stream);
  ENVOY_LOG(trace, "decodeData returning {}", status);
  return status;
}

FilterTrailersStatus Filter::onTrailers(ProcessorState& state, Http::HeaderMap& trailers) {
  if (processing_complete_) {
    ENVOY_LOG(trace, "trailers: Continue");
    return FilterTrailersStatus::Continue;
  }

  bool body_delivered = state.completeBodyAvailable();
  state.setCompleteBodyAvailable(true);
  state.setTrailersAvailable(true);
  state.setTrailers(&trailers);

  if (state.callbackState() == ProcessorState::CallbackState::HeadersCallback ||
      state.callbackState() == ProcessorState::CallbackState::BufferedBodyCallback) {
    ENVOY_LOG(trace, "Previous callback still executing -- holding header iteration");
    return FilterTrailersStatus::StopIteration;
  }

  if (!body_delivered && state.bodyMode() == ProcessingMode::BUFFERED) {
    // We would like to process the body in a buffered way, but until now the complete
    // body has not arrived. With the arrival of trailers, we now know that the body
    // has arrived.
    sendBufferedData(state, ProcessorState::CallbackState::BufferedBodyCallback, true);
    return FilterTrailersStatus::StopIteration;
  }

  if (!state.sendTrailers()) {
    ENVOY_LOG(trace, "Skipped trailer processing");
    return FilterTrailersStatus::Continue;
  }

  switch (openStream()) {
  case StreamOpenState::Error:
    return FilterTrailersStatus::StopIteration;
  case StreamOpenState::IgnoreError:
    return FilterTrailersStatus::Continue;
  case StreamOpenState::Ok:
    // Fall through
    break;
  }

  sendTrailers(state, trailers);
  return FilterTrailersStatus::StopIteration;
}

FilterTrailersStatus Filter::decodeTrailers(RequestTrailerMap& trailers) {
  ENVOY_LOG(trace, "decodeTrailers");
  const auto status = onTrailers(decoding_state_, trailers);
  ENVOY_LOG(trace, "encodeTrailers returning {}", status);
  return status;
}

FilterHeadersStatus Filter::encodeHeaders(ResponseHeaderMap& headers, bool end_stream) {
  ENVOY_LOG(trace, "encodeHeaders end_stream = {}", end_stream);
  if (end_stream) {
    encoding_state_.setCompleteBodyAvailable(true);
  }

  if (processing_complete_ || !encoding_state_.sendHeaders()) {
    ENVOY_LOG(trace, "encodeHeaders: Continue");
    return FilterHeadersStatus::Continue;
  }

  const auto status = onHeaders(encoding_state_, headers, end_stream);
  ENVOY_LOG(trace, "encodeHeaders returns {}", status);
  return status;
}

FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "encodeData({}): end_stream = {}", data.length(), end_stream);
  const auto status = onData(encoding_state_, data, end_stream);
  ENVOY_LOG(trace, "encodeData returning {}", status);
  return status;
}

FilterTrailersStatus Filter::encodeTrailers(ResponseTrailerMap& trailers) {
  ENVOY_LOG(trace, "encodeTrailers");
  const auto status = onTrailers(encoding_state_, trailers);
  ENVOY_LOG(trace, "encodeTrailers returning {}", status);
  return status;
}

void Filter::sendBodyChunk(ProcessorState& state, const Buffer::Instance& data,
                           ProcessorState::CallbackState new_state, bool end_stream) {
  ENVOY_LOG(debug, "Sending a body chunk of {} bytes", data.length());
  state.setCallbackState(new_state);
  state.startMessageTimer(std::bind(&Filter::onMessageTimeout, this), config_->messageTimeout());
  ProcessingRequest req;
  auto* body_req = state.mutableBody(req);
  body_req->set_end_of_stream(end_stream);
  body_req->set_body(data.toString());
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
}

void Filter::sendTrailers(ProcessorState& state, const Http::HeaderMap& trailers) {
  ProcessingRequest req;
  auto* trailers_req = state.mutableTrailers(req);
  MutationUtils::headersToProto(trailers, *trailers_req->mutable_trailers());
  state.setCallbackState(ProcessorState::CallbackState::TrailersCallback);
  state.startMessageTimer(std::bind(&Filter::onMessageTimeout, this), config_->messageTimeout());
  ENVOY_LOG(debug, "Sending trailers message");
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
}

void Filter::onReceiveMessage(std::unique_ptr<ProcessingResponse>&& r) {
  if (processing_complete_) {
    ENVOY_LOG(debug, "Ignoring stream message received after processing complete");
    // Ignore additional messages after we decided we were done with the stream
    return;
  }

  auto response = std::move(r);
  bool message_handled = false;

  // Update processing mode now because filter callbacks check it
  // and the various "handle" methods below may result in callbacks
  // being invoked in line.
  if (response->has_mode_override()) {
    ENVOY_LOG(debug, "Processing mode overridden by server for this request");
    decoding_state_.setProcessingMode(response->mode_override());
    encoding_state_.setProcessingMode(response->mode_override());
  }

  switch (response->response_case()) {
  case ProcessingResponse::ResponseCase::kRequestHeaders:
    ENVOY_LOG(debug, "Received RequestHeaders response");
    message_handled = decoding_state_.handleHeadersResponse(response->request_headers());
    break;
  case ProcessingResponse::ResponseCase::kResponseHeaders:
    ENVOY_LOG(debug, "Received ResponseHeaders response");
    message_handled = encoding_state_.handleHeadersResponse(response->response_headers());
    break;
  case ProcessingResponse::ResponseCase::kRequestBody:
    ENVOY_LOG(debug, "Received RequestBody response");
    message_handled = decoding_state_.handleBodyResponse(response->request_body());
    break;
  case ProcessingResponse::ResponseCase::kResponseBody:
    ENVOY_LOG(debug, "Received ResponseBody response");
    message_handled = encoding_state_.handleBodyResponse(response->response_body());
    break;
  case ProcessingResponse::ResponseCase::kRequestTrailers:
    ENVOY_LOG(debug, "Received RequestTrailers response");
    message_handled = decoding_state_.handleTrailersResponse(response->request_trailers());
    break;
  case ProcessingResponse::ResponseCase::kResponseTrailers:
    ENVOY_LOG(debug, "Received responseTrailers response");
    message_handled = encoding_state_.handleTrailersResponse(response->response_trailers());
    break;
  case ProcessingResponse::ResponseCase::kImmediateResponse:
    ENVOY_LOG(debug, "Received ImmediateResponse response");
    // We won't be sending anything more to the stream after we
    // receive this message.
    processing_complete_ = true;
    sendImmediateResponse(response->immediate_response());
    message_handled = true;
    break;
  default:
    // Any other message is considered spurious
    ENVOY_LOG(debug, "Received unknown stream message {} -- ignoring and marking spurious",
              response->response_case());
    break;
  }

  if (message_handled) {
    stats_.stream_msgs_received_.inc();
  } else {
    stats_.spurious_msgs_received_.inc();
    // When a message is received out of order, ignore it and also
    // ignore the stream for the rest of this filter instance's lifetime
    // to protect us from a malformed server.
    ENVOY_LOG(warn, "Spurious response message {} received on gRPC stream",
              response->response_case());
    clearAsyncState();
    processing_complete_ = true;
  }
}

void Filter::onGrpcError(Grpc::Status::GrpcStatus status) {
  ENVOY_LOG(debug, "Received gRPC error on stream: {}", status);
  stats_.streams_failed_.inc();

  if (processing_complete_) {
    return;
  }

  if (config_->failureModeAllow()) {
    // Ignore this and treat as a successful close
    onGrpcClose();
    stats_.failure_mode_allowed_.inc();

  } else {
    processing_complete_ = true;
    // Since the stream failed, there is no need to handle timeouts, so
    // make sure that they do not fire now.
    cleanUpTimers();
    ImmediateResponse errorResponse;
    errorResponse.mutable_status()->set_code(envoy::type::v3::StatusCode::InternalServerError);
    errorResponse.set_details(absl::StrFormat("%s: gRPC error %i", kErrorPrefix, status));
    sendImmediateResponse(errorResponse);
  }
}

void Filter::onGrpcClose() {
  ENVOY_LOG(debug, "Received gRPC stream close");
  processing_complete_ = true;
  stats_.streams_closed_.inc();
  // Successful close. We can ignore the stream for the rest of our request
  // and response processing.
  clearAsyncState();
}

void Filter::onMessageTimeout() {
  ENVOY_LOG(debug, "message timeout reached");
  stats_.message_timeouts_.inc();
  if (config_->failureModeAllow()) {
    // The user would like a timeout to not cause message processing to fail.
    // However, we don't know if the external processor will send a response later,
    // and we can't wait any more. So, as we do for a spurious message, ignore
    // the external processor for the rest of the request.
    processing_complete_ = true;
    stats_.failure_mode_allowed_.inc();
    clearAsyncState();

  } else {
    // Return an error and stop processing the current stream.
    processing_complete_ = true;
    decoding_state_.setCallbackState(ProcessorState::CallbackState::Idle);
    encoding_state_.setCallbackState(ProcessorState::CallbackState::Idle);
    ImmediateResponse errorResponse;
    errorResponse.mutable_status()->set_code(envoy::type::v3::StatusCode::InternalServerError);
    errorResponse.set_details(absl::StrFormat("%s: per-message timeout exceeded", kErrorPrefix));
    sendImmediateResponse(errorResponse);
  }
}

// Regardless of the current filter state, reset it to "IDLE", continue
// the current callback, and reset timers. This is used in a few error-handling situations.
void Filter::clearAsyncState() {
  decoding_state_.clearAsyncState();
  encoding_state_.clearAsyncState();
}

// Regardless of the current state, ensure that the timers won't fire
// again.
void Filter::cleanUpTimers() {
  decoding_state_.cleanUpTimer();
  encoding_state_.cleanUpTimer();
}

void Filter::sendImmediateResponse(const ImmediateResponse& response) {
  const auto status_code = response.has_status() ? response.status().code() : 200;
  const auto grpc_status =
      response.has_grpc_status()
          ? absl::optional<Grpc::Status::GrpcStatus>(response.grpc_status().status())
          : absl::nullopt;
  const auto mutate_headers = [&response](Http::ResponseHeaderMap& headers) {
    if (response.has_headers()) {
      MutationUtils::applyHeaderMutations(response.headers(), headers, false);
    }
  };

  sent_immediate_response_ = true;
  encoder_callbacks_->sendLocalReply(static_cast<Http::Code>(status_code), response.body(),
                                     mutate_headers, grpc_status, response.details());
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
