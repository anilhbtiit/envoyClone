#pragma once

#include "envoy/server/factory_context.h"
#include "envoy/server/instance.h"

#include "source/server/factory_context_impl.h"

namespace Envoy {
namespace Server {

class AdminFactoryContext final : public FactoryContextImplBase {
public:
  AdminFactoryContext(Envoy::Server::Instance& server)
      : FactoryContextImplBase(server, server.stats().createScope(""),
                               server.stats().createScope("listener.admin."),
                               envoy::config::core::v3::Metadata::default_instance(),
                               envoy::config::core::v3::UNSPECIFIED, false) {}

  ProtobufMessage::ValidationVisitor& messageValidationVisitor() const override {
    // Always use the static validation visitor for the admin handler.
    return server_.messageValidationContext().staticValidationVisitor();
  }
  Init::Manager& initManager() override {
    // Reuse the server init manager to avoid creating a new one for this special listener.
    return server_.initManager();
  }
  Network::DrainDecision& drainDecision() override {
    // Reuse the server drain manager to avoid creating a new one for this special listener.
    return server_.drainManager();
  }
};
using AdminFactoryContextPtr = std::unique_ptr<AdminFactoryContext>;

} // namespace Server
} // namespace Envoy
