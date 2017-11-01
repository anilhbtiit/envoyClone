#pragma once

#include <functional>

#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Ssl {

/**
 * Manages all of the SSL contexts in the process
 */
class ContextManager {
public:
  virtual ~ContextManager() {}

  /**
   * Builds a ClientContext from a ClientContextConfig.
   */
  virtual ClientContextPtr createSslClientContext(Stats::Scope& scope,
                                                  ClientContextConfig& config) PURE;

  /**
   * Builds a ServerContext from a ServerContextConfig.
   */
  virtual ServerContextPtr createSslServerContext(const std::string& listener_name,
                                                  const std::vector<std::string>& server_names,
                                                  Stats::Scope& scope,
                                                  ServerContextConfig& config) PURE;

  /**
   * Find ServerContext for a given listener and server_name.
   * @return ServerContext or nullptr in case there is no match.
   */
  virtual ServerContext* findSslServerContext(const std::string& listener_name,
                                              const std::string& server_name) PURE;

  /**
   * @return the number of days until the next certificate being managed will expire.
   */
  virtual size_t daysUntilFirstCertExpires() PURE;

  /**
   * Iterate through all currently allocated contexts.
   */
  virtual void iterateContexts(std::function<void(Context&)> callback) PURE;
};

} // namespace Ssl
} // namespace Envoy
