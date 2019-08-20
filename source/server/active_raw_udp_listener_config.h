#pragma once

#include "envoy/network/connection_handler.h"
#include "envoy/registry/registry.h"
#include "envoy/server/active_udp_listener_config.h"

namespace Envoy {
namespace Server {

class ActiveRawUdpListenerFactory : public Network::ActiveUdpListenerFactory {
  Network::ConnectionHandler::ActiveListenerPtr
  createActiveUdpListener(Network::ConnectionHandler& parent,
                          Network::ListenerConfig& config) const override;
};

// This class uses a protobuf config to create a UDP listener factory which
// creates a Server::ConnectionHandlerImpl::ActiveUdpListener.
// This is the default UDP listener if not specified in config.
class ActiveRawUdpListenerConfigFactory : public ActiveUdpListenerConfigFactory {

  Network::ActiveUdpListenerFactoryPtr
  createActiveUdpListenerFactory(const Protobuf::Message&) override;

  std::string name() override;
};

DECLARE_FACTORY(ActiveRawUdpListenerConfigFactory);

} // namespace Server
} // namespace Envoy
