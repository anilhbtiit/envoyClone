#pragma once

#include <google/protobuf/descriptor.h>

#include <memory>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/service/accesslog/v3/als.pb.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/assert.h"
#include "source/common/grpc/buffered_async_client.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/access_loggers/common/grpc_access_logger_utils.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Common {

enum class GrpcAccessLoggerType { TCP, HTTP };

namespace Detail {

/**
 * Fully specialized types of the interfaces below are available through the
 * `Common::GrpcAccessLogger::Interface` and `Common::GrpcAccessLoggerCache::interface`
 * aliases.
 */

/**
 * Interface for an access logger. The logger provides abstraction on top of gRPC stream, deals with
 * reconnects and performs batching.
 */
template <typename HttpLogProto, typename TcpLogProto> class GrpcAccessLogger {
public:
  using SharedPtr = std::shared_ptr<GrpcAccessLogger>;

  virtual ~GrpcAccessLogger() = default;

  /**
   * Log http access entry.
   * @param entry supplies the access log to send.
   */
  virtual void log(HttpLogProto&& entry) PURE;

  /**
   * Log tcp access entry.
   * @param entry supplies the access log to send.
   */
  virtual void log(TcpLogProto&& entry) PURE;

  /**
   * Critical HTTP log entry.
   * @param entry supplies the access log to send.
   */
  virtual void criticalLog(HttpLogProto&& entry) PURE;

  /**
   * Critical TCP log entry.
   * @param entry supplies the access log to send.
   */
  virtual void criticalLog(TcpLogProto&& entry) PURE;
};

/**
 * Interface for an access logger cache. The cache deals with threading and de-duplicates loggers
 * for the same configuration.
 */
template <typename GrpcAccessLogger, typename ConfigProto> class GrpcAccessLoggerCache {
public:
  using SharedPtr = std::shared_ptr<GrpcAccessLoggerCache>;
  virtual ~GrpcAccessLoggerCache() = default;

  /**
   * Get existing logger or create a new one for the given configuration.
   * @param config supplies the configuration for the logger.
   * @return GrpcAccessLoggerSharedPtr ready for logging requests.
   */
  virtual typename GrpcAccessLogger::SharedPtr
  getOrCreateLogger(const ConfigProto& config, GrpcAccessLoggerType logger_type) PURE;
};

template <typename LogRequest, typename LogResponse> class GrpcAccessLogClient {
public:
  GrpcAccessLogClient(const Grpc::RawAsyncClientSharedPtr& client,
                      const Protobuf::MethodDescriptor& service_method,
                      OptRef<const envoy::config::core::v3::RetryPolicy> retry_policy)
      : client_(client), service_method_(service_method), grpc_stream_retry_policy_(retry_policy) {}

public:
  struct LocalStream : public Grpc::AsyncStreamCallbacks<LogResponse> {
    LocalStream(GrpcAccessLogClient& parent) : parent_(parent) {}

    // Grpc::AsyncStreamCallbacks
    void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
    void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
    void onReceiveMessage(std::unique_ptr<LogResponse>&&) override {}
    void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
    void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override {
      ASSERT(parent_.stream_ != nullptr);
      if (parent_.stream_->stream_ != nullptr) {
        // Only reset if we have a stream. Otherwise we had an inline failure and we will clear the
        // stream data in send().
        parent_.stream_.reset();
      }
    }

    GrpcAccessLogClient& parent_;
    Grpc::AsyncStream<LogRequest> stream_{};
  };

  bool isStreamStarted() { return stream_ != nullptr && stream_->stream_ != nullptr; }

  bool log(const LogRequest& request) {
    if (!stream_) {
      stream_ = std::make_unique<LocalStream>(*this);
    }

    if (stream_->stream_ == nullptr) {
      stream_->stream_ = client_->start(service_method_, *stream_, createStreamOptionsForRetry());
    }

    if (stream_->stream_ != nullptr) {
      if (stream_->stream_->isAboveWriteBufferHighWatermark()) {
        return false;
      }
      stream_->stream_->sendMessage(request, false);
    } else {
      // Clear out the stream data due to stream creation failure.
      stream_.reset();
    }
    return true;
  }

  Http::AsyncClient::StreamOptions createStreamOptionsForRetry() {
    auto opt = Http::AsyncClient::StreamOptions();

    if (!grpc_stream_retry_policy_) {
      return opt;
    }

    const auto retry_policy =
        Http::Utility::convertCoreToRouteRetryPolicy(*grpc_stream_retry_policy_, "connect-failure");
    opt.setBufferBodyForRetry(true);
    opt.setRetryPolicy(retry_policy);
    return opt;
  }

  Grpc::AsyncClient<LogRequest, LogResponse> client_;
  std::unique_ptr<LocalStream> stream_;
  const Protobuf::MethodDescriptor& service_method_;
  const absl::optional<envoy::config::core::v3::RetryPolicy> grpc_stream_retry_policy_;
};

constexpr absl::string_view GRPC_LOG_STATS_PREFIX = "access_logs.grpc_access_log.";

#define CRITICAL_ACCESS_LOGGER_GRPC_CLIENT_STATS(COUNTER, GAUGE)                                   \
  COUNTER(critical_logs_nack_received)                                                             \
  COUNTER(critical_logs_ack_received)

struct GrpcCriticalAccessLogClientGrpcClientStats {
  CRITICAL_ACCESS_LOGGER_GRPC_CLIENT_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

template <class RequestType, class ResponseType> class GrpcCriticalAccessLogClient {
public:
  struct CriticalLogStreamCallbacks : public Grpc::AsyncStreamCallbacks<ResponseType> {
    explicit CriticalLogStreamCallbacks(GrpcCriticalAccessLogClient& parent) : parent_(parent) {}

    // Grpc::AsyncStreamCallbacks
    void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
    void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
    void onReceiveMessage(std::unique_ptr<ResponseType>&& message) override {
      const auto& id = message->id();

      switch (message->status()) {
      case envoy::service::accesslog::v3::CriticalAccessLogsResponse::ACK:
        parent_.stats_.critical_logs_ack_received_.inc();
        parent_.buffered_client_.onSuccess(id);
        break;
      case envoy::service::accesslog::v3::CriticalAccessLogsResponse::NACK:
        parent_.stats_.critical_logs_nack_received_.inc();
        parent_.buffered_client_.onError(id);
        break;
      default:
        return;
      }
    }
    void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
    void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override {}

    GrpcCriticalAccessLogClient& parent_;
  };

  GrpcCriticalAccessLogClient(const Grpc::RawAsyncClientSharedPtr& client,
                              const Protobuf::MethodDescriptor& method,
                              Event::Dispatcher& dispatcher, Stats::Scope& scope,
                              const std::string& log_name, /* ??? */
                              std::chrono::milliseconds message_ack_timeout,
                              uint64_t max_pending_buffer_size_bytes)
      : stats_({CRITICAL_ACCESS_LOGGER_GRPC_CLIENT_STATS(
            POOL_COUNTER_PREFIX(scope, GRPC_LOG_STATS_PREFIX.data()),
            POOL_GAUGE_PREFIX(scope, GRPC_LOG_STATS_PREFIX.data()))}),
        log_name_(log_name), stream_callbacks_(*this),
        buffered_client_(max_pending_buffer_size_bytes, method, stream_callbacks_,
                         Grpc::AsyncClient<RequestType, ResponseType>(client), dispatcher,
                         message_ack_timeout, scope) {}

  void flush(RequestType& message) {
    auto id = buffered_client_.bufferMessage(message);
    if (!id.has_value()) {
      return;
    }
    message.set_id(*id);
    buffered_client_.sendBufferedMessages();
  }

  bool isStreamStarted() { return buffered_client_.hasActiveStream(); }

private:
  friend CriticalLogStreamCallbacks;

  GrpcCriticalAccessLogClientGrpcClientStats stats_;
  const std::string log_name_;
  CriticalLogStreamCallbacks stream_callbacks_;
  Grpc::BufferedAsyncClient<RequestType, ResponseType> buffered_client_;
};

template <class RequestType, class ResponseType>
using GrpcCriticalAccessLogClientPtr =
    std::unique_ptr<GrpcCriticalAccessLogClient<RequestType, ResponseType>>;

} // namespace Detail

/**
 * All stats for the grpc access logger. @see stats_macros.h
 */
#define ALL_GRPC_ACCESS_LOGGER_STATS(COUNTER)                                                      \
  COUNTER(logs_written)                                                                            \
  COUNTER(logs_dropped)

/**
 * Wrapper struct for the access log stats. @see stats_macros.h
 */
struct GrpcAccessLoggerStats {
  ALL_GRPC_ACCESS_LOGGER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Base class for defining a gRPC logger with the `HttpLogProto` and `TcpLogProto` access log
 * entries and `LogRequest` and `LogResponse` gRPC messages.
 * The log entries and messages are distinct types to support batching of multiple access log
 * entries in a single gRPC messages that go on the wire.
 */
template <typename HttpLogProto, typename TcpLogProto, typename LogRequest, typename LogResponse>
class GrpcAccessLogger : public Detail::GrpcAccessLogger<HttpLogProto, TcpLogProto> {
public:
  using Interface = Detail::GrpcAccessLogger<HttpLogProto, TcpLogProto>;

  GrpcAccessLogger(
      const Grpc::RawAsyncClientSharedPtr& client,
      const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
      Event::Dispatcher& dispatcher, Stats::Scope& scope, std::string access_log_prefix,
      const Protobuf::MethodDescriptor& service_method)
      : client_(client, service_method, GrpcCommon::optionalRetryPolicy(config)),
        buffer_flush_interval_msec_(
            PROTOBUF_GET_MS_OR_DEFAULT(config, buffer_flush_interval, 1000)),
        max_buffer_size_bytes_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, buffer_size_bytes, 16384)),
        flush_timer_(dispatcher.createTimer([this]() {
          flush();
          flush_timer_->enableTimer(buffer_flush_interval_msec_);
        })),
        stats_({ALL_GRPC_ACCESS_LOGGER_STATS(POOL_COUNTER_PREFIX(scope, access_log_prefix))}) {
    flush_timer_->enableTimer(buffer_flush_interval_msec_);
  }

  void log(HttpLogProto&& entry) override {
    if (!canLogMore()) {
      return;
    }
    approximate_message_size_bytes_ += entry.ByteSizeLong();
    addEntry(std::move(entry));
    if (approximate_message_size_bytes_ >= max_buffer_size_bytes_) {
      flush();
    }
  }

  void log(TcpLogProto&& entry) override {
    approximate_message_size_bytes_ += entry.ByteSizeLong();
    addEntry(std::move(entry));
    if (approximate_message_size_bytes_ >= max_buffer_size_bytes_) {
      flush();
    }
  }

  virtual void criticalLog(HttpLogProto&&) override {}

  virtual void criticalLog(TcpLogProto&&) override {}

protected:
  Detail::GrpcAccessLogClient<LogRequest, LogResponse> client_;
  LogRequest message_;
  const std::chrono::milliseconds buffer_flush_interval_msec_;
  const uint64_t max_buffer_size_bytes_;

private:
  virtual bool isEmpty() PURE;
  virtual void initMessage() PURE;
  virtual void addEntry(HttpLogProto&& entry) PURE;
  virtual void addEntry(TcpLogProto&& entry) PURE;
  virtual void clearMessage() { message_.Clear(); }

  virtual void flush() {
    if (isEmpty()) {
      // Nothing to flush.
      return;
    }

    if (!client_.isStreamStarted()) {
      initMessage();
    }

    if (client_.log(message_)) {
      // Clear the message regardless of the success.
      approximate_message_size_bytes_ = 0;
      clearMessage();
    }
  }

  bool canLogMore() {
    if (max_buffer_size_bytes_ == 0 || approximate_message_size_bytes_ < max_buffer_size_bytes_) {
      stats_.logs_written_.inc();
      return true;
    }
    flush();
    if (approximate_message_size_bytes_ < max_buffer_size_bytes_) {
      stats_.logs_written_.inc();
      return true;
    }
    stats_.logs_dropped_.inc();
    return false;
  }

  const Event::TimerPtr flush_timer_;
  uint64_t approximate_message_size_bytes_ = 0;
  GrpcAccessLoggerStats stats_;
};

template <typename HttpLogProto, typename TcpLogProto, typename LogRequest, typename LogResponse,
          typename CriticalLogRequest, typename CriticalLogResponse>
class GrpcCriticalAccessLogger
    : public GrpcAccessLogger<HttpLogProto, TcpLogProto, LogRequest, LogResponse> {
public:
  using Base = GrpcAccessLogger<HttpLogProto, TcpLogProto, LogRequest, LogResponse>;

  GrpcCriticalAccessLogger(
      const Grpc::RawAsyncClientSharedPtr& client,
      const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
      Event::Dispatcher& dispatcher, Stats::Scope& scope, std::string access_log_prefix,
      const Protobuf::MethodDescriptor& service_method,
      const Protobuf::MethodDescriptor& critical_service_method)
      : Base(client, config, dispatcher, scope, access_log_prefix, service_method) {
    if (config.has_critical_buffer_log_filter()) {
      critical_client_ = std::make_unique<
          Detail::GrpcCriticalAccessLogClient<CriticalLogRequest, CriticalLogResponse>>(
          client, critical_service_method, dispatcher, scope, access_log_prefix,
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(config, message_ack_timeout, 5000)),
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_pending_buffer_size_bytes, 16384));

      critical_flush_timer_ = dispatcher.createTimer([this] {
        flushCritical();
        critical_flush_timer_->enableTimer(Base::buffer_flush_interval_msec_);
      });
      critical_flush_timer_->enableTimer(Base::buffer_flush_interval_msec_);
    }
  }

  void criticalLog(HttpLogProto&& entry) override {
    approximate_critical_message_size_bytes_ += entry.ByteSizeLong();
    addCriticalEntry(std::move(entry));
    if (approximate_critical_message_size_bytes_ >= Base::max_buffer_size_bytes_) {
      flushCritical();
    }
  }

  void criticalLog(TcpLogProto&&) override {}

protected:
  Detail::GrpcCriticalAccessLogClientPtr<CriticalLogRequest, CriticalLogResponse> critical_client_;
  CriticalLogRequest critical_message_;

private:
  virtual void addCriticalEntry(HttpLogProto&& entry) PURE;
  virtual void addCriticalEntry(TcpLogProto&& entry) PURE;
  virtual void initCriticalMessage() PURE;
  virtual bool isCriticalMessageEmpty() PURE;
  virtual void clearCriticalMessage() PURE;

  void flushCritical() {
    if (critical_client_ == nullptr && isCriticalMessageEmpty()) {
      return;
    }

    if (!critical_client_->isStreamStarted()) {
      initCriticalMessage();
    }

    critical_client_->flush(critical_message_);
    approximate_critical_message_size_bytes_ = 0;
    critical_message_.Clear();
  }

  uint64_t approximate_critical_message_size_bytes_ = 0;
  Event::TimerPtr critical_flush_timer_;
};

/**
 * Class for defining logger cache with the `GrpcAccessLogger` interface and
 * `ConfigProto` configuration.
 */
template <typename GrpcAccessLogger, typename ConfigProto>
class GrpcAccessLoggerCache : public Singleton::Instance,
                              public Detail::GrpcAccessLoggerCache<GrpcAccessLogger, ConfigProto> {
public:
  using Interface = Detail::GrpcAccessLoggerCache<GrpcAccessLogger, ConfigProto>;

  GrpcAccessLoggerCache(Grpc::AsyncClientManager& async_client_manager, Stats::Scope& scope,
                        ThreadLocal::SlotAllocator& tls)
      : scope_(scope), async_client_manager_(async_client_manager), tls_slot_(tls.allocateSlot()) {
    tls_slot_->set([](Event::Dispatcher& dispatcher) {
      return std::make_shared<ThreadLocalCache>(dispatcher);
    });
  }

  typename GrpcAccessLogger::SharedPtr
  getOrCreateLogger(const ConfigProto& config, GrpcAccessLoggerType logger_type) override {
    // TODO(euroelessar): Consider cleaning up loggers.
    auto& cache = tls_slot_->getTyped<ThreadLocalCache>();
    const auto cache_key = std::make_pair(MessageUtil::hash(config), logger_type);
    const auto it = cache.access_loggers_.find(cache_key);
    if (it != cache.access_loggers_.end()) {
      return it->second;
    }
    // We pass skip_cluster_check=true to factoryForGrpcService in order to avoid throwing
    // exceptions in worker threads. Call sites of this getOrCreateLogger must check the cluster
    // availability via ClusterManager::checkActiveStaticCluster beforehand, and throw exceptions in
    // the main thread if necessary.
    auto client = async_client_manager_.factoryForGrpcService(config.grpc_service(), scope_, true)
                      ->createUncachedRawAsyncClient();
    const auto logger = createLogger(config, std::move(client), cache.dispatcher_);
    cache.access_loggers_.emplace(cache_key, logger);
    return logger;
  }

protected:
  Stats::Scope& scope_;

private:
  /**
   * Per-thread cache.
   */
  struct ThreadLocalCache : public ThreadLocal::ThreadLocalObject {
    ThreadLocalCache(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

    Event::Dispatcher& dispatcher_;
    // Access loggers indexed by the hash of logger's configuration and logger type.
    absl::flat_hash_map<std::pair<std::size_t, Common::GrpcAccessLoggerType>,
                        typename GrpcAccessLogger::SharedPtr>
        access_loggers_;
  };

  // Create the specific logger type for this cache.
  virtual typename GrpcAccessLogger::SharedPtr
  createLogger(const ConfigProto& config, const Grpc::RawAsyncClientSharedPtr& client,
               Event::Dispatcher& dispatcher) PURE;

  Grpc::AsyncClientManager& async_client_manager_;
  ThreadLocal::SlotPtr tls_slot_;
};

} // namespace Common
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
