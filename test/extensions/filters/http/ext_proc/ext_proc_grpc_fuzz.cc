#include "test/extensions/filters/http/ext_proc/ext_proc_grpc_fuzz.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;

DEFINE_PROTO_FUZZER(const test::extensions::filters::http::ext_proc::ExtProcGrpcTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  // have separate data providers.
  FuzzedDataProvider downstream_provider(
      reinterpret_cast<const uint8_t*>(input.downstream_data().data()),
      input.downstream_data().size());
  FuzzedDataProvider ext_proc_provider(
      reinterpret_cast<const uint8_t*>(input.ext_proc_data().data()), input.ext_proc_data().size());

  // Get IP and gRPC version from environment
  ExtProcIntegrationFuzz fuzzer(TestEnvironment::getIpVersionsForTest()[0],
                                TestEnvironment::getsGrpcVersionsForTest()[0]);
  ExtProcFuzzHelper fuzz_helper(&ext_proc_provider);

  // This starts an external processor in a separate thread. This allows for the
  // external process to consume messages in a loop without blocking the fuzz
  // target from receiving the response.
  fuzzer.test_processor_.start(
      fuzzer.ip_version_,
      [&fuzz_helper](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        while (true) {
          ProcessingRequest req;
          if (!stream->Read(&req)) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected message");
          }

          fuzz_helper.logRequest(&req);

          // The following blocks generate random data for the 9 fields of the
          // ProcessingResponse gRPC message

          // 1 - 7. Randomize response
          // If true, immediately close the connection with a random Grpc Status.
          // Otherwise randomize the response
          ProcessingResponse resp;
          if (fuzz_helper.provider_->ConsumeBool()) {
            ENVOY_LOG_MISC(trace, "Immediately Closing gRPC connection");
            return fuzz_helper.randomGrpcStatusWithMessage();
          } else {
            ENVOY_LOG_MISC(trace, "Generating Random ProcessingResponse");
            fuzz_helper.randomizeResponse(&resp, &req);
          }

          // 8. Randomize dynamic_metadata
          // TODO(ikepolinsky): ext_proc does not support dynamic_metadata

          // 9. Randomize mode_override
          if (fuzz_helper.provider_->ConsumeBool()) {
            ENVOY_LOG_MISC(trace, "Generating Random ProcessingMode Override");
            ProcessingMode* msg = resp.mutable_mode_override();
            fuzz_helper.randomizeOverrideResponse(msg);
          }

          ENVOY_LOG_MISC(trace, "Response generated, writing to stream.");
          stream->Write(resp);
        }

        return grpc::Status::OK;
      });

  ENVOY_LOG_MISC(trace, "External Process started.");

  fuzzer.initializeFuzzer(true);
  ENVOY_LOG_MISC(trace, "Fuzzer initialized");

  const auto response = fuzzer.randomDownstreamRequest(&downstream_provider, &fuzz_helper);

  // For fuzz testing we don't care about the response code, only that
  // the stream ended in some graceful manner
  ENVOY_LOG_MISC(trace, "Waiting for response.");
  if (response->waitForEndStream(std::chrono::milliseconds(200))) {
    ENVOY_LOG_MISC(trace, "Response received.");
  } else {
    // TODO(ikepolinsky): investigate if there is anyway around this.
    // Waiting too long for a fuzz case to fail will drastically
    // reduce executions/second.
    ENVOY_LOG_MISC(trace, "Response timed out.");
  }
  fuzzer.tearDown();
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
