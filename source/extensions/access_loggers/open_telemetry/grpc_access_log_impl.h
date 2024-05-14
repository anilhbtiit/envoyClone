#pragma once

#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/common/grpc_access_logger.h"

#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

// Note: OpenTelemetry protos are extra flexible and used also in the OT collector for batching and
// so forth. As a result, some fields are repeated, but for our use case we assume the following
// structure:
// ExportLogsServiceRequest -> (single) ResourceLogs -> (single) ScopeLogs ->
// (repeated) LogRecord.
class GrpcAccessLoggerImpl
    : public Common::GrpcAccessLogger<
          opentelemetry::proto::logs::v1::LogRecord,
          // OpenTelemetry logging uses LogRecord for both HTTP and TCP, so protobuf::Empty is used
          // as an empty placeholder for the non-used addEntry method.
          // TODO(itamarkam): Don't cache OpenTelemetry loggers by type (HTTP/TCP).
          ProtobufWkt::Empty, opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest,
          opentelemetry::proto::collector::logs::v1::ExportLogsServiceResponse> {
public:
  GrpcAccessLoggerImpl(
      const Grpc::RawAsyncClientSharedPtr& client,
      const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
          config,
      Event::Dispatcher& dispatcher, const LocalInfo::LocalInfo& local_info, Stats::Scope& scope);

  ~GrpcAccessLoggerImpl() { *destructing_ = true; }

private:
  void initMessageRoot(
      const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
          config,
      const LocalInfo::LocalInfo& local_info);
  // Extensions::AccessLoggers::GrpcCommon::GrpcAccessLogger
  void addEntry(opentelemetry::proto::logs::v1::LogRecord&& entry) override;
  // Non used addEntry method (the above is used for both TCP and HTTP).
  void addEntry(ProtobufWkt::Empty&& entry) override { (void)entry; };
  bool isEmpty() override;
  void initMessage() override;
  void clearMessage() override;

  class OTelLogRequestCallbacks
      : public Grpc::AsyncRequestCallbacks<
            opentelemetry::proto::collector::logs::v1::ExportLogsServiceResponse>,
        public Envoy::Event::DeferredDeletable {
  public:
    OTelLogRequestCallbacks(Envoy::Event::Dispatcher& dispatcher,
                            Common::GrpcAccessLoggerStats& stats, uint32_t sending_log_entries,
                            std::shared_ptr<bool> destructing)
        : dispatcher_(dispatcher), stats_(stats), sending_log_entries_(sending_log_entries),
          destructing_(destructing) {}

    void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}

    void onSuccess(Grpc::ResponsePtr<
                       opentelemetry::proto::collector::logs::v1::ExportLogsServiceResponse>&& resp,
                   Tracing::Span&) override {
      if (!*destructing_) {
        int partial_rejected_log_entries = (resp && resp->has_partial_success())
                                               ? resp->partial_success().rejected_log_records()
                                               : 0;
        stats_.logs_dropped_.add(partial_rejected_log_entries);
        stats_.logs_written_.add(sending_log_entries_ - partial_rejected_log_entries);
      }
      deferredDelete();
    }

    void onFailure(Grpc::Status::GrpcStatus, const std::string&, Tracing::Span&) override {
      if (!*destructing_) {
        stats_.logs_dropped_.add(sending_log_entries_);
      }
      deferredDelete();
    }

    void deferredDelete() {
      dispatcher_.deferredDelete(std::unique_ptr<OTelLogRequestCallbacks>(this));
    }

    Envoy::Event::Dispatcher& dispatcher_;
    Common::GrpcAccessLoggerStats& stats_;
    uint32_t sending_log_entries_;
    std::shared_ptr<bool> destructing_;
  };

  opentelemetry::proto::logs::v1::ScopeLogs* root_;
  Common::GrpcAccessLoggerStats stats_;
  std::shared_ptr<bool> destructing_ = std::make_shared<bool>(false);
  uint32_t batched_log_entries_ = 0;
};

class GrpcAccessLoggerCacheImpl
    : public Common::GrpcAccessLoggerCache<
          GrpcAccessLoggerImpl,
          envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig> {
public:
  GrpcAccessLoggerCacheImpl(Grpc::AsyncClientManager& async_client_manager, Stats::Scope& scope,
                            ThreadLocal::SlotAllocator& tls,
                            const LocalInfo::LocalInfo& local_info);

private:
  // Common::GrpcAccessLoggerCache
  GrpcAccessLoggerImpl::SharedPtr createLogger(
      const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
          config,
      Event::Dispatcher& dispatcher) override;

  const LocalInfo::LocalInfo& local_info_;
};

/**
 * Aliases for class interfaces for mock definitions.
 */
using GrpcAccessLogger = GrpcAccessLoggerImpl::Interface;
using GrpcAccessLoggerSharedPtr = GrpcAccessLogger::SharedPtr;

using GrpcAccessLoggerCache = GrpcAccessLoggerCacheImpl::Interface;
using GrpcAccessLoggerCacheSharedPtr = GrpcAccessLoggerCache::SharedPtr;

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
