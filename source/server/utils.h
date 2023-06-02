#pragma once

#include "envoy/admin/v3/server_info.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/init/manager.h"
#include "envoy/server/options.h"

namespace Envoy {
namespace Server {
namespace Utility {

/*
    Fetches the current state of the server (e.g., initializing, live, etc.)
    given the manager's state and the status of the health check.
*/
envoy::admin::v3::ServerInfo::State serverState(Init::Manager::State state,
                                                bool health_check_failed);

void assertExclusiveLogFormatMethod(const Options& options,
                                    const envoy::config::bootstrap::v3::Bootstrap& bootstrap);

void maybeSetApplicationLogFormat(const envoy::config::bootstrap::v3::Bootstrap& bootstrap);

} // namespace Utility
} // namespace Server
} // namespace Envoy
