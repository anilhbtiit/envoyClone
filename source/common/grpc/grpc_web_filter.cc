#include "common/grpc/grpc_web_filter.h"

#include <arpa/inet.h>

#include "common/common/base64.h"
#include "common/common/utility.h"
#include "common/http/headers.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Grpc {

// Bit mask denotes a trailers frame of gRPC-Web.
const uint8_t GrpcWebFilter::GRPC_WEB_TRAILER = 0b10000000;

// Supported gRPC-Web content-types.
const std::vector<std::string>& GrpcWebFilter::GRPC_WEB_CONTENT_TYPES() const {
  static const std::vector<std::string> types{
      Http::Headers::get().ContentTypeValues.GrpcWeb,
      Http::Headers::get().ContentTypeValues.GrpcWebProto,
      Http::Headers::get().ContentTypeValues.GrpcWebText,
      Http::Headers::get().ContentTypeValues.GrpcWebTextProto};
  return types;
}

bool GrpcWebFilter::isGrpcWebRequest(const Http::HeaderMap& headers) {
  const Http::HeaderEntry* content_type = headers.ContentType();
  if (content_type != nullptr) {
    for (const std::string& valid : GrpcWebFilter::GRPC_WEB_CONTENT_TYPES()) {
      if (valid == content_type->value().c_str()) {
        return true;
      }
    }
  }
  return false;
}

// Implements StreamDecoderFilter.
// TODO(fengli): Implements the subtypes of gRPC-Web content-type other than proto, like +json, etc.
Http::FilterHeadersStatus GrpcWebFilter::decodeHeaders(Http::HeaderMap& headers, bool) {
  const Http::HeaderEntry* content_type = headers.ContentType();
  if (!isGrpcWebRequest(headers)) {
    // TODO(fengli): Return error response.
    return Http::FilterHeadersStatus::StopIteration;
  }

  setupStatTracking(headers);

  if (content_type != nullptr &&
      (Http::Headers::get().ContentTypeValues.GrpcWebText == content_type->value().c_str() ||
       Http::Headers::get().ContentTypeValues.GrpcWebTextProto == content_type->value().c_str())) {
    // Checks whether gRPC-Web client is sending base64 encoded request.
    is_text_request_ = true;
  }
  headers.insertContentType().value(Http::Headers::get().ContentTypeValues.Grpc);

  const Http::HeaderEntry* accept = headers.get(Http::Headers::get().Accept);
  if (accept != nullptr &&
      (Http::Headers::get().ContentTypeValues.GrpcWebText == accept->value().c_str() ||
       Http::Headers::get().ContentTypeValues.GrpcWebTextProto == accept->value().c_str())) {
    // Checks whether gRPC-Web client is asking for base64 encoded response.
    is_text_response_ = true;
  }

  // Adds te:trailers to upstream HTTP2 request. It's required for gRPC.
  headers.insertTE().value(Http::Headers::get().TEValues.Trailers);
  // Adds grpc-accept-encoding:identity,deflate,gzip. It's required for gRPC.
  headers.insertGrpcAcceptEncoding().value(Http::Headers::get().GrpcAcceptEncodingValues.Default);
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus GrpcWebFilter::decodeData(Buffer::Instance& data, bool) {
  if (!is_text_request_) {
    // No additional transcoding required if gRPC client is sending binary request.
    return Http::FilterDataStatus::Continue;
  }

  // Parse application/grpc-web-text format.
  if (data.length() + decoding_buffer_.length() < 4) {
    decoding_buffer_.move(data);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  const uint64_t needed =
      (data.length() + decoding_buffer_.length()) / 4 * 4 - decoding_buffer_.length();
  decoding_buffer_.move(data, needed);
  const std::string decoded = Base64::decode(
      std::string(static_cast<const char*>(decoding_buffer_.linearize(decoding_buffer_.length())),
                  decoding_buffer_.length()));
  decoding_buffer_.drain(decoding_buffer_.length());
  decoding_buffer_.move(data);
  data.add(decoded);
  return Http::FilterDataStatus::Continue;
}

// Implements StreamEncoderFilter.
Http::FilterHeadersStatus GrpcWebFilter::encodeHeaders(Http::HeaderMap& headers, bool) {
  if (do_stat_tracking_) {
    chargeStat(headers);
  }
  if (is_text_response_) {
    headers.insertContentType().value(Http::Headers::get().ContentTypeValues.GrpcWebTextProto);
  } else {
    headers.insertContentType().value(Http::Headers::get().ContentTypeValues.GrpcWebProto);
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus GrpcWebFilter::encodeData(Buffer::Instance& data, bool) {
  if (!is_text_response_) {
    // No additional transcoding required if gRPC-Web client asked for binary response.
    return Http::FilterDataStatus::Continue;
  }

  // The decoder always consumes and drains the given buffer. Incomplete data frame is buffered
  // inside the decoder.
  std::vector<Frame> frames;
  decoder_.decode(data, frames);
  if (frames.empty()) {
    // We don't have enough data to decode for one single frame, stop iteration until more data
    // comes in.
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  // Encodes the decoded gRPC frames with base64.
  for (auto& frame : frames) {
    Buffer::OwnedImpl temp;
    temp.add(&frame.flags_, 1);
    const uint32_t length = htonl(frame.length_);
    temp.add(&length, 4);
    if (frame.length_ > 0) {
      temp.add(*frame.data_);
    }
    data.add(Base64::encode(temp, temp.length()));
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus GrpcWebFilter::encodeTrailers(Http::HeaderMap& trailers) {
  if (do_stat_tracking_) {
    chargeStat(trailers);
  }

  // Trailers are expected to come all in once, and will be encoded into one single trailers frame.
  // Trailers in the trailers frame are separated by CRLFs.
  Buffer::OwnedImpl temp;
  trailers.iterate([](const Http::HeaderEntry& header, void* context) -> void {
    Buffer::Instance* temp = static_cast<Buffer::Instance*>(context);
    temp->add(header.key().c_str(), header.key().size());
    temp->add(":");
    temp->add(header.value().c_str(), header.value().size());
    temp->add("\r\n");
  }, &temp);
  Buffer::OwnedImpl buffer;
  // Adds the trailers frame head.
  buffer.add(&GRPC_WEB_TRAILER, 1);
  // Adds the trailers frame length.
  const uint32_t length = htonl(temp.length());
  buffer.add(&length, 4);
  buffer.move(temp);
  if (is_text_response_) {
    Buffer::OwnedImpl encoded(Base64::encode(buffer, buffer.length()));
    encoder_callbacks_->addEncodedData(encoded);
  } else {
    encoder_callbacks_->addEncodedData(buffer);
  }
  return Http::FilterTrailersStatus::Continue;
}

void GrpcWebFilter::setupStatTracking(const Http::HeaderMap& headers) {
  Router::RouteConstSharedPtr route = decoder_callbacks_->route();
  if (!route || !route->routeEntry()) {
    return;
  }

  const Router::RouteEntry* route_entry = route->routeEntry();
  Upstream::ThreadLocalCluster* cluster = cm_.get(route_entry->clusterName());
  if (!cluster) {
    return;
  }
  cluster_ = cluster->info();

  if (headers.Path() != nullptr && headers.Path()->value().c_str() != nullptr) {
    std::vector<std::string> parts = StringUtil::split(headers.Path()->value().c_str(), '/');
    if (parts.size() != 2) {
      return;
    }
    grpc_service_ = parts[0];
    grpc_method_ = parts[1];
    do_stat_tracking_ = true;
  }
}

void GrpcWebFilter::chargeStat(const Http::HeaderMap& headers) {
  const Http::HeaderEntry* grpc_status_header = headers.GrpcStatus();
  if (!grpc_status_header) {
    return;
  }
  uint64_t grpc_status_code;
  bool success = StringUtil::atoul(grpc_status_header->value().c_str(), grpc_status_code) &&
                 grpc_status_code == 0;
  // TODO(fengli): Add fine grained stat for gRPC status.
  cluster_->statsScope()
      .counter(fmt::format("grpc-web.{}.{}.{}", grpc_service_, grpc_method_,
                           success ? "success" : "failure"))
      .inc();
  cluster_->statsScope()
      .counter(fmt::format("grpc-web.{}.{}.total", grpc_service_, grpc_method_))
      .inc();
}

} // namespace Grpc
} // namespace Envoy
