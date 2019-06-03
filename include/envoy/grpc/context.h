#pragma once

#include <memory>

#include "envoy/http/header_map.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Grpc {

/**
 * Captures http-related structures with cardinality of one per server.
 */
class Context {
public:
  virtual ~Context() = default;

  enum class Protocol { Grpc, GrpcWeb };

  struct RequestNames;

  virtual absl::optional<RequestNames> resolveServiceAndMethod(const Http::HeaderEntry* path) PURE;

  /**
   * Charge a success/failure stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param protocol supplies the downstream protocol in use.
   * @param request_names supplies the request names.
   * @param grpc_status supplies the gRPC status.
   */
  virtual void chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                          const RequestNames& request_names,
                          const Http::HeaderEntry* grpc_status) PURE;

  /**
   * Charge a success/failure stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param protocol supplies the downstream protocol in use.
   * @param request_names supplies the request names.
   * @param success supplies whether the call succeeded.
   */
  virtual void chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                          const RequestNames& request_names, bool success) PURE;

  /**
   * Charge a success/failure stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param request_names supplies the request names.
   * @param success supplies whether the call succeeded.
   */
  virtual void chargeStat(const Upstream::ClusterInfo& cluster, const RequestNames& request_names,
                          bool success) PURE;
  ;
};

using ContextPtr = std::unique_ptr<Context>;

} // namespace Grpc
} // namespace Envoy
