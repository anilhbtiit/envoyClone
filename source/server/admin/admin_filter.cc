#include "source/server/admin/admin_filter.h"

#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

AdminFilter::AdminFilter(AdminServerCallbackFunction admin_server_callback_func)
    : admin_server_callback_func_(admin_server_callback_func) {}

Http::FilterHeadersStatus AdminFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                     bool end_stream) {
  request_headers_ = &headers;
  if (end_stream) {
    onComplete();
  }

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus AdminFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  // Currently we generically buffer all admin request data in case a handler wants to use it.
  // If we ever support streaming admin requests we may need to revisit this. Note, we must use
  // addDecodedData() here since we might need to perform onComplete() processing if end_stream is
  // true.
  decoder_callbacks_->addDecodedData(data, false);

  if (end_stream) {
    onComplete();
  }

  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus AdminFilter::decodeTrailers(Http::RequestTrailerMap&) {
  onComplete();
  return Http::FilterTrailersStatus::StopIteration;
}

void AdminFilter::onDestroy() {
  for (const auto& callback : on_destroy_callbacks_) {
    callback();
  }
}

void AdminFilter::addOnDestroyCallback(std::function<void()> cb) {
  on_destroy_callbacks_.push_back(std::move(cb));
}

Http::StreamDecoderFilterCallbacks& AdminFilter::getDecoderFilterCallbacks() const {
  ASSERT(decoder_callbacks_ != nullptr);
  return *decoder_callbacks_;
}

const Buffer::Instance* AdminFilter::getRequestBody() const {
  return decoder_callbacks_->decodingBuffer();
}

const Http::RequestHeaderMap& AdminFilter::getRequestHeaders() const {
  ASSERT(request_headers_ != nullptr);
  return *request_headers_;
}

void AdminFilter::onComplete() {
  const absl::string_view path = request_headers_->getPathValue();
  ENVOY_STREAM_LOG(debug, "request complete: path: {}", *decoder_callbacks_, path);

  Buffer::OwnedImpl response;
  auto header_map = Http::ResponseHeaderMapImpl::create();
  RELEASE_ASSERT(request_headers_, "");
  for (bool cont = true, first = true; cont;) {
    // TODO(jmarantz): admin_server-callback_func_ is only going to access
    // header_map on the first iteration. This clang-tidy suppression can be
    // cleaned up once the handler mechanism provides a separate "nextChunk"
    // interface that does not take a header_map.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    Http::Code code = admin_server_callback_func_(path, *header_map, response, *this);
    cont = code == Http::Code::Continue;
    bool end_stream = end_stream_on_complete_ && !cont;
    if (first) {
      Utility::populateFallbackResponseHeaders(cont ? Http::Code::OK : code, *header_map);
      decoder_callbacks_->encodeHeaders(std::move(header_map), end_stream && response.length() == 0,
                                        StreamInfo::ResponseCodeDetails::get().AdminFilterResponse);
      first = false;
    }
    if (response.length() > 0) {
      // ENVOY_LOG_MISC(error, "Chunking out {} bytes cont={}", response.length(), cont);
      decoder_callbacks_->encodeData(response, end_stream);
    }
  }
}

} // namespace Server
} // namespace Envoy
