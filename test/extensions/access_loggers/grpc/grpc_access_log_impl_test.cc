#include <chrono>
#include <memory>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/service/accesslog/v3/als.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/extensions/access_loggers/grpc/http_grpc_access_log_impl.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {
namespace {

constexpr std::chrono::milliseconds FlushInterval(10);
constexpr int BUFFER_SIZE_BYTES = 0;

// A helper test class to mock and intercept GrpcAccessLoggerImpl streams.
class GrpcAccessLoggerImplTestHelper {
public:
  using MockAccessLogStream = Grpc::MockAsyncStream;
  using AccessLogCallbacks =
      Grpc::AsyncStreamCallbacks<envoy::service::accesslog::v3::StreamAccessLogsResponse>;

  GrpcAccessLoggerImplTestHelper(LocalInfo::MockLocalInfo& local_info,
                                 Grpc::MockAsyncClient* async_client) {
    EXPECT_CALL(local_info, node());
    EXPECT_CALL(*async_client, startRaw(_, _, _, _))
        .WillOnce(
            Invoke([this](absl::string_view, absl::string_view, Grpc::RawAsyncStreamCallbacks& cbs,
                          const Http::AsyncClient::StreamOptions&) {
              this->callbacks_ = dynamic_cast<AccessLogCallbacks*>(&cbs);
              return &this->stream_;
            }));
  }

  void expectStreamMessage(const std::string& expected_message_yaml) {
    envoy::service::accesslog::v3::StreamAccessLogsMessage expected_message;
    TestUtility::loadFromYaml(expected_message_yaml, expected_message);
    EXPECT_CALL(stream_, isAboveWriteBufferHighWatermark()).WillOnce(Return(false));
    EXPECT_CALL(stream_, sendMessageRaw_(_, false))
        .WillOnce(Invoke([expected_message](Buffer::InstancePtr& request, bool) {
          envoy::service::accesslog::v3::StreamAccessLogsMessage message;
          Buffer::ZeroCopyInputStreamImpl request_stream(std::move(request));
          EXPECT_TRUE(message.ParseFromZeroCopyStream(&request_stream));
          EXPECT_EQ(message.DebugString(), expected_message.DebugString());
        }));
  }

  void expectStreamCriticalMessage(const std::string& expected_message_yaml) {
    envoy::service::accesslog::v3::CriticalAccessLogsMessage expected_message;
    TestUtility::loadFromYaml(expected_message_yaml, expected_message);
    EXPECT_CALL(stream_, isAboveWriteBufferHighWatermark()).WillOnce(Return(false));
    EXPECT_CALL(stream_, sendMessageRaw_(_, false))
        .WillOnce(Invoke([expected_message](Buffer::InstancePtr& request, bool) {
          envoy::service::accesslog::v3::CriticalAccessLogsMessage message;
          Buffer::ZeroCopyInputStreamImpl request_stream(std::move(request));
          EXPECT_TRUE(message.ParseFromZeroCopyStream(&request_stream));
          message.set_id(0);
          EXPECT_EQ(message.DebugString(), expected_message.DebugString());
        }));
  }

private:
  MockAccessLogStream stream_;
  AccessLogCallbacks* callbacks_;
};

class GrpcAccessLoggerImplTest : public testing::Test {
public:
  GrpcAccessLoggerImplTest()
      : async_client_(new Grpc::MockAsyncClient),
        timer_buffer_flusher_(new Event::MockTimer(&dispatcher_)),
        grpc_access_logger_impl_test_helper_(local_info_, async_client_) {
    // enableTimer in timer_ttl_manager_ is never called due to the empty critical message.
    EXPECT_CALL(*timer_buffer_flusher_, enableTimer(_, _));

    *config_.mutable_log_name() = "test_log_name";
    config_.mutable_buffer_size_bytes()->set_value(BUFFER_SIZE_BYTES);
    config_.mutable_buffer_flush_interval()->set_nanos(
        std::chrono::duration_cast<std::chrono::nanoseconds>(FlushInterval).count());
  }

  void initialize(bool enable_critical) {
    if (enable_critical) {
      const std::string filter_yaml = R"EOF(
status_code_filter:
  comparison:
    op: EQ
    value:
      default_value: 200
      runtime_key: access_log.access_error.status
    )EOF";

      envoy::config::accesslog::v3::AccessLogFilter filter_config;
      TestUtility::loadFromYaml(filter_yaml, filter_config);
      *config_.mutable_critical_buffer_log_filter() = filter_config;

      timer_ttl_manager_ = std::make_unique<Event::MockTimer>(&dispatcher_);
      EXPECT_CALL(*timer_ttl_manager_, enableTimer(_, _));
      EXPECT_CALL(*timer_ttl_manager_, enabled());
      EXPECT_CALL(*timer_ttl_manager_, disableTimer());

      timer_critical_flusher_ = std::make_unique<Event::MockTimer>(&dispatcher_);
      EXPECT_CALL(*timer_critical_flusher_, enableTimer(_, _));
    }

    logger_ = std::make_unique<GrpcAccessLoggerImpl>(
        Grpc::RawAsyncClientPtr{async_client_}, config_, dispatcher_, local_info_, stats_store_);
  }

  Grpc::MockAsyncClient* async_client_;
  Stats::IsolatedStoreImpl stats_store_;
  LocalInfo::MockLocalInfo local_info_;
  Event::MockDispatcher dispatcher_;
  std::unique_ptr<Event::MockTimer> timer_ttl_manager_;
  std::unique_ptr<Event::MockTimer> timer_critical_flusher_;
  Event::MockTimer* timer_buffer_flusher_;
  std::unique_ptr<GrpcAccessLoggerImpl> logger_;
  GrpcAccessLoggerImplTestHelper grpc_access_logger_impl_test_helper_;
  envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig config_;
};

TEST_F(GrpcAccessLoggerImplTest, LogHttp) {
  initialize(false);

  grpc_access_logger_impl_test_helper_.expectStreamMessage(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
  log_name: test_log_name
http_logs:
  log_entry:
    request:
      path: /test/path1
)EOF");
  envoy::data::accesslog::v3::HTTPAccessLogEntry entry;
  entry.mutable_request()->set_path("/test/path1");
  logger_->log(envoy::data::accesslog::v3::HTTPAccessLogEntry(entry));
}

TEST_F(GrpcAccessLoggerImplTest, LogTcp) {
  initialize(false);

  grpc_access_logger_impl_test_helper_.expectStreamMessage(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
  log_name: test_log_name
tcp_logs:
  log_entry:
    common_properties:
      sample_rate: 1.0
)EOF");
  envoy::data::accesslog::v3::TCPAccessLogEntry tcp_entry;
  tcp_entry.mutable_common_properties()->set_sample_rate(1);
  logger_->log(envoy::data::accesslog::v3::TCPAccessLogEntry(tcp_entry));
}

TEST_F(GrpcAccessLoggerImplTest, CriticalLogHttp) {
  initialize(true);
  grpc_access_logger_impl_test_helper_.expectStreamCriticalMessage(R"EOF(
message:
  identifier:
    node:
      id: node_name
      cluster: cluster_name
      locality:
        zone: zone_name
    log_name: test_log_name
  http_logs:
    log_entry:
      request:
        path: /test/path1
id: 0
)EOF");
  envoy::data::accesslog::v3::HTTPAccessLogEntry entry;
  entry.mutable_request()->set_path("/test/path1");
  logger_->criticalLog(envoy::data::accesslog::v3::HTTPAccessLogEntry(entry));
}

class GrpcAccessLoggerCacheImplTest : public testing::Test {
public:
  GrpcAccessLoggerCacheImplTest()
      : async_client_(new Grpc::MockAsyncClient), factory_(new Grpc::MockAsyncClientFactory),
        logger_cache_(async_client_manager_, scope_, tls_, local_info_),
        grpc_access_logger_impl_test_helper_(local_info_, async_client_) {
    EXPECT_CALL(async_client_manager_, factoryForGrpcService(_, _, true))
        .WillOnce(Invoke([this](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
          EXPECT_CALL(*factory_, createUncachedRawAsyncClient()).WillOnce(Invoke([this] {
            return Grpc::RawAsyncClientPtr{async_client_};
          }));
          return Grpc::AsyncClientFactoryPtr{factory_};
        }));
  }

  Grpc::MockAsyncClient* async_client_;
  Grpc::MockAsyncClientFactory* factory_;
  Grpc::MockAsyncClientManager async_client_manager_;
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  LocalInfo::MockLocalInfo local_info_;
  GrpcAccessLoggerCacheImpl logger_cache_;
  GrpcAccessLoggerImplTestHelper grpc_access_logger_impl_test_helper_;
};

// Test that the logger is created according to the config (by inspecting the generated log).
TEST_F(GrpcAccessLoggerCacheImplTest, LoggerCreation) {
  envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig config;
  config.set_log_name("test-log");
  config.set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
  // Force a flush for every log entry.
  config.mutable_buffer_size_bytes()->set_value(BUFFER_SIZE_BYTES);

  GrpcAccessLoggerSharedPtr logger =
      logger_cache_.getOrCreateLogger(config, Common::GrpcAccessLoggerType::HTTP);
  // Note that the local info node() method is mocked, so the node is not really configurable.
  grpc_access_logger_impl_test_helper_.expectStreamMessage(R"EOF(
  identifier:
    node:
      id: node_name
      cluster: cluster_name
      locality:
        zone: zone_name
    log_name: test-log
  http_logs:
    log_entry:
      request:
        path: /test/path1
  )EOF");
  envoy::data::accesslog::v3::HTTPAccessLogEntry entry;
  entry.mutable_request()->set_path("/test/path1");
  logger->log(envoy::data::accesslog::v3::HTTPAccessLogEntry(entry));
}

} // namespace
} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
