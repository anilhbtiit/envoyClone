#pragma once

#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/common/optional.h"
#include "envoy/grpc/status.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/message.h"
#include "envoy/stats/stats.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Grpc {

class Exception : public EnvoyException {
public:
  Exception(const Optional<uint64_t>& grpc_status, const std::string& message)
      : EnvoyException(message), grpc_status_(grpc_status) {}

  const Optional<uint64_t> grpc_status_;
};

class Common {
public:
  /**
   * Returns the GrpcStatus code from a given set of headers, if present.
   * @param headers the headers to parse.
   * @return Optional<Status::GrpcStatus> the parsed status code or InvalidCode if no valid status
   *         is found.
   */
  static Optional<Status::GrpcStatus> getGrpcStatus(const Http::HeaderMap& headers);

  /**
   * Returns the grpc-message from a given set of headers, if present.
   * @param headers the headers to parse.
   * @return std::string the gRPC status message or empty string if grpc-message is not present in
   *         headers.
   */
  static std::string getGrpcMessage(const Http::HeaderMap& headers);

  /**
   * Returns the gRPC status code from a given HTTP response status code. Ordinarily, it is expected
   * that a 200 response is provided, but gRPC defines a mapping for intermediaries that are not
   * gRPC aware, see https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md.
   * @param http_response_status HTTP status code.
   * @return Status::GrpcStatus corresponding gRPC status code.
   */
  static Status::GrpcStatus httpToGrpcStatus(uint64_t http_response_status);

  /**
   * Charge a success/failure stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param protocol supplies the downstream protocol in use, either gRPC or gRPC-Web.
   * @param grpc_service supplies the service name.
   * @param grpc_method supplies the method name.
   * @param grpc_status supplies the gRPC status.
   */
  static void chargeStat(const Upstream::ClusterInfo& cluster, const std::string& protocol,
                         const std::string& grpc_service, const std::string& grpc_method,
                         const Http::HeaderEntry* grpc_status);

  /**
   * Charge a success/failure stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param protocol supplies the downstream protocol in use, either "grpc" or "grpc-web".
   * @param grpc_service supplies the service name.
   * @param grpc_method supplies the method name.
   * @param success supplies whether the call succeeded.
   */
  static void chargeStat(const Upstream::ClusterInfo& cluster, const std::string& protocol,
                         const std::string& grpc_service, const std::string& grpc_method,
                         bool success);

  /**
   * Charge a success/failure stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param grpc_service supplies the service name.
   * @param grpc_method supplies the method name.
   * @param success supplies whether the call succeeded.
   */
  static void chargeStat(const Upstream::ClusterInfo& cluster, const std::string& grpc_service,
                         const std::string& grpc_method, bool success);

  /**
   * Resolve the gRPC service and method from the HTTP2 :path header.
   * @param path supplies the :path header.
   * @param service supplies the output pointer of the gRPC service.
   * @param method supplies the output pointer of the gRPC method.
   * @return bool true if both gRPC serve and method have been resolved successfully.
   */
  static bool resolveServiceAndMethod(const Http::HeaderEntry* path, std::string* service,
                                      std::string* method);

  /**
   * Serialize protobuf message.
   */
  static Buffer::InstancePtr serializeBody(const Protobuf::Message& message);

  /**
   * Prepare headers for protobuf service.
   */
  static Http::MessagePtr prepareHeaders(const std::string& upstream_cluster,
                                         const std::string& service_full_name,
                                         const std::string& method_name);

  /**
   * Basic validation of gRPC response, @throws Grpc::Exception in case of non successful response.
   */
  static void validateResponse(Http::Message& http_response);

  static const std::string GRPC_CONTENT_TYPE;

private:
  static void checkForHeaderOnlyError(Http::Message& http_response);
};

} // namespace Grpc
} // namespace Envoy
