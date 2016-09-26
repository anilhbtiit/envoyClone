#include "common.h"
#include "rpc_channel_impl.h"
#include "utility.h"

#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

#include "google/protobuf/message.h"

namespace Grpc {

void RpcChannelImpl::cancel() {
  http_request_->cancel();
  onComplete();
}

void RpcChannelImpl::CallMethod(const proto::MethodDescriptor* method, proto::RpcController*,
                                const proto::Message* grpc_request, proto::Message* grpc_response,
                                proto::Closure*) {
  ASSERT(!http_request_ && !grpc_method_ && !grpc_response_);
  grpc_method_ = method;
  grpc_response_ = grpc_response;

  // For proto3 messages this should always return true.
  ASSERT(grpc_request->IsInitialized());

  // This should be caught in configuration, and a request will fail normally anyway, but assert
  // here for clarity.
  ASSERT(cm_.get(cluster_)->features() & Upstream::Cluster::Features::HTTP2);

  Http::MessagePtr message = Utility::prepareHeaders(cluster_, method->service()->full_name(), method->name());
  message->body(Utility::serializeBody(*grpc_request));

  callbacks_.onPreRequestCustomizeHeaders(message->headers());
  http_request_ = cm_.httpAsyncClientForCluster(cluster_).send(std::move(message), *this, timeout_);
}

void RpcChannelImpl::incStat(bool success) {
  Common::chargeStat(stats_store_, cluster_, grpc_method_->service()->full_name(),
                     grpc_method_->name(), success);
}

void RpcChannelImpl::checkForHeaderOnlyError(Http::Message& http_response) {
  // First check for grpc-status in headers. If it is here, we have an error.
  const std::string& grpc_status_header = http_response.headers().get(Common::GRPC_STATUS_HEADER);
  if (grpc_status_header.empty()) {
    return;
  }

  uint64_t grpc_status_code;
  if (!StringUtil::atoul(grpc_status_header.c_str(), grpc_status_code)) {
    throw Exception(Optional<uint64_t>(), "bad grpc-status header");
  }

  const std::string& grpc_status_message = http_response.headers().get(Common::GRPC_MESSAGE_HEADER);
  throw Exception(grpc_status_code, grpc_status_message);
}

void RpcChannelImpl::onSuccessWorker(Http::Message& http_response) {
  if (Http::Utility::getResponseStatus(http_response.headers()) != enumToInt(Http::Code::OK)) {
    throw Exception(Optional<uint64_t>(), "non-200 response code");
  }

  checkForHeaderOnlyError(http_response);

  // Check for existance of trailers.
  if (!http_response.trailers()) {
    throw Exception(Optional<uint64_t>(), "no response trailers");
  }

  const std::string& grpc_status_header = http_response.trailers()->get(Common::GRPC_STATUS_HEADER);
  const std::string& grpc_status_message =
      http_response.trailers()->get(Common::GRPC_MESSAGE_HEADER);
  uint64_t grpc_status_code;
  if (!StringUtil::atoul(grpc_status_header.c_str(), grpc_status_code)) {
    throw Exception(Optional<uint64_t>(), "bad grpc-status trailer");
  }

  if (grpc_status_code != 0) {
    throw Exception(grpc_status_code, grpc_status_message);
  }

  // A GRPC response contains a 5 byte header. Currently we only support unary responses so we
  // ignore the header. @see serializeBody().
  if (!http_response.body() || !(http_response.body()->length() > 5)) {
    throw Exception(Optional<uint64_t>(), "bad serialized body");
  }

  http_response.body()->drain(5);
  if (!grpc_response_->ParseFromString(http_response.bodyAsString())) {
    throw Exception(Optional<uint64_t>(), "bad serialized body");
  }
}

void RpcChannelImpl::onSuccess(Http::MessagePtr&& http_response) {
  try {
    onSuccessWorker(*http_response);
    callbacks_.onSuccess();
    incStat(true);
    onComplete();
  } catch (const Exception& e) {
    onFailureWorker(e.grpc_status_, e.what());
  }
}

void RpcChannelImpl::onFailureWorker(const Optional<uint64_t>& grpc_status,
                                     const std::string& message) {
  callbacks_.onFailure(grpc_status, message);
  incStat(false);
  onComplete();
}

void RpcChannelImpl::onFailure(Http::AsyncClient::FailureReason reason) {
  switch (reason) {
  case Http::AsyncClient::FailureReason::Reset:
    onFailureWorker(Optional<uint64_t>(), "stream reset");
    break;
  }
}

void RpcChannelImpl::onComplete() {
  http_request_ = nullptr;
  grpc_method_ = nullptr;
  grpc_response_ = nullptr;
}

void RpcAsyncClientImpl::send(const std::string& upstream_cluster,
                              const std::string& service_full_name,
                              const std::string& method_name,
                              proto::Message&& grpc_request,
                              Http::AsyncClient::Callbacks& callbacks) {
  // For proto3 messages this should always return true.
  ASSERT(grpc_request->IsInitialized());
  // This should be caught in configuration, and a request will fail normally anyway, but assert
  // here for clarity.
  ASSERT(cm_.get(cluster_)->features() & Upstream::Cluster::Features::HTTP2);

  Http::MessagePtr message = Utility::prepareHeaders(upstream_cluster, service_full_name, method_name);
  message->body(Utility::serializeBody(grpc_request));

  // TODO: pass timeout to send method.
  cm_.httpAsyncClientForCluster(upstream_cluster)
      .send(std::move(message), callbacks, std::chrono::milliseconds(5000));
}

} // Grpc
