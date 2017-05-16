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
  NetworkFilterFactoryCb tryCreateFilterFactory(NetworkFilterType type, const std::string& name,
                                                const Json::Object& json_config,
                                                Server::Instance& server);
};

} // Configuration
} // Server
} // Envoy
