#pragma once

#include <string>

#include "server/configuration_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the client SSL auth filter. @see NetworkFilterConfigFactory.
 */
class ClientSslAuthConfigFactory : public NetworkFilterConfigFactory {
public:
  // NetworkFilterConfigFactory
  NetworkFilterFactoryCb createFilterFactory(NetworkFilterType type,
                                             const Json::Object& json_config,
                                             Server::Instance& server) override;
  std::string name() override;
};

} // Configuration
} // Server
} // Envoy
