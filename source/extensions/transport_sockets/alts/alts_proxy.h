#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "grpcpp/channel.h"
#include "grpcpp/client_context.h"
#include "grpcpp/support/sync_stream.h"
#include "src/proto/grpc/gcp/handshaker.grpc.pb.h"
#include "src/proto/grpc/gcp/handshaker.pb.h"
#include "src/proto/grpc/gcp/transport_security_common.pb.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

constexpr char kApplicationProtocol[] = "grpc";
constexpr char kRecordProtocol[] = "ALTSRP_GCM_AES128_REKEY";
constexpr std::size_t kMaxFrameSize = 1024 * 1024;
constexpr std::size_t kMaxMajorRpcVersion = 2;
constexpr std::size_t kMaxMinorRpcVersion = 1;
constexpr std::size_t kMinMajorRpcVersion = 2;
constexpr std::size_t kMinMinorRpcVersion = 1;

// Manages a bidirectional stream to the ALTS handshaker service. An AltsProxy
// instance is tied to a single ALTS handshake and must not be reused.
class AltsProxy {
public:
  static absl::StatusOr<std::unique_ptr<AltsProxy>>
  Create(std::shared_ptr<grpc::Channel> handshaker_service_channel);

  ~AltsProxy();

  // Sends a StartClientHandshakeReq message to the ALTS handshaker service and
  // returns the response. This API is blocking.
  absl::StatusOr<grpc::gcp::HandshakerResp> SendStartClientHandshakeReq();

  // Sends a StartServerHandshakeReq message to the ALTS handshaker service and
  // returns the response. This API is blocking.
  absl::StatusOr<grpc::gcp::HandshakerResp> SendStartServerHandshakeReq(absl::string_view in_bytes);

  // Sends a NextHandshakeMessageReq message to the ALTS handshaker service and
  // returns the response. This API is blocking.
  absl::StatusOr<grpc::gcp::HandshakerResp> SendNextHandshakeReq(absl::string_view in_bytes);

private:
  static void SetRpcProtocolVersions(grpc::gcp::RpcProtocolVersions* rpc_protocol_versions);

  AltsProxy(
      std::unique_ptr<grpc::ClientContext> client_context,
      std::unique_ptr<grpc::gcp::HandshakerService::Stub> stub,
      std::unique_ptr<grpc::ClientReaderWriter<grpc::gcp::HandshakerReq, grpc::gcp::HandshakerResp>>
          stream);

  std::unique_ptr<grpc::ClientContext> client_context_ = nullptr;
  std::unique_ptr<grpc::gcp::HandshakerService::Stub> stub_;
  std::unique_ptr<grpc::ClientReaderWriter<grpc::gcp::HandshakerReq, grpc::gcp::HandshakerResp>>
      stream_ = nullptr;
};

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
