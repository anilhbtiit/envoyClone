#pragma once

#include <chrono>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.validate.h"
#include "envoy/extensions/health_checkers/thrift/v3/thrift.pb.h"

#include "source/common/upstream/health_checker_base_impl.h"
#include "source/extensions/filters/network/thrift_proxy/config.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

/**
 * Thrift health checker implementation.
 */
class ThriftHealthChecker : public Upstream::HealthCheckerImplBase {
public:
  ThriftHealthChecker(const Upstream::Cluster& cluster,
                      const envoy::config::core::v3::HealthCheck& config,
                      const envoy::extensions::health_checkers::thrift::v3::Thrift& thrift_config,
                      Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                      Upstream::HealthCheckEventLoggerPtr&& event_logger, Api::Api& api);

protected:
  envoy::data::core::v3::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v3::THRIFT;
  }

private:
  // friend class ThriftHealthCheckerTest;

  struct ThriftActiveHealthCheckSession : public ActiveHealthCheckSession,
                                          public Network::ConnectionCallbacks {
    ThriftActiveHealthCheckSession(ThriftHealthChecker& parent,
                                   const Upstream::HostSharedPtr& host);
    ~ThriftActiveHealthCheckSession() override;

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;
    void onDeferredDelete() final;

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    ThriftHealthChecker& parent_;

    // Extensions::NetworkFilters::Common::Thrift::ThriftCommandStatsSharedPtr
    // thrift_command_stats_;
  };

  using ThriftActiveHealthCheckSessionPtr = std::unique_ptr<ThriftActiveHealthCheckSession>;

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(Upstream::HostSharedPtr host) override {
    return std::make_unique<ThriftActiveHealthCheckSession>(*this, host);
  }

  const std::string method_name_;
};

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
