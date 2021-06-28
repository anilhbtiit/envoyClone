#pragma once

#include "envoy/server/filter_config.h"
#include "envoy/server/instance.h"

namespace Envoy {
namespace Server {

/**
 * Implementation of FactoryContext wrapping a Server::Instance and some listener components.
 */
class FactoryContextImpl : public Configuration::FactoryContext {
public:
  FactoryContextImpl(Server::Instance& server, const envoy::config::listener::v3::Listener& config,
                     Network::DrainDecision& drain_decision, Stats::Scope& global_scope,
                     Stats::Scope& listener_scope);

  // Configuration::FactoryContext
  AccessLog::AccessLogManager& accessLogManager() override;
  Upstream::ClusterManager& clusterManager() override;
  Event::Dispatcher& dispatcher() override;
  const Server::Options& options() override;
  Grpc::Context& grpcContext() override;
  Router::Context& routerContext() override;
  bool healthCheckFailed() override;
  Http::Context& httpContext() override;
  Init::Manager& initManager() override;
  const LocalInfo::LocalInfo& localInfo() const override;
  Envoy::Runtime::Loader& runtime() override;
  Stats::Scope& scope() override;
  Singleton::Manager& singletonManager() override;
  OverloadManager& overloadManager() override;
  ThreadLocal::SlotAllocator& threadLocal() override;
  Admin& admin() override;
  TimeSource& timeSource() override;
  ProtobufMessage::ValidationContext& messageValidationContext() override;
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override;
  Api::Api& api() override;
  ServerLifecycleNotifier& lifecycleNotifier() override;
  ProcessContextOptRef processContext() override;
  Configuration::ServerFactoryContext& getServerFactoryContext() const override;
  Configuration::TransportSocketFactoryContext& getTransportSocketFactoryContext() const override;
  const envoy::config::core::v3::Metadata& listenerMetadata() const override;
  envoy::config::core::v3::TrafficDirection direction() const override;
  Network::DrainDecision& drainDecision() override;
  Stats::Scope& listenerScope() override;

private:
  Server::Instance& server_;
  const envoy::config::listener::v3::Listener& config_;
  Network::DrainDecision& drain_decision_;
  Stats::Scope& global_scope_;
  Stats::Scope& listener_scope_;
};

} // namespace Server
} // namespace Envoy
