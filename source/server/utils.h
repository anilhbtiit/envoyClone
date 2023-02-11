#pragma once

#include "envoy/admin/v3/server_info.pb.h"
#include "envoy/init/manager.h"

namespace Envoy {
namespace Server {
namespace Utility {

/*
    Fetches the current state of the server (e.g., initializing, live, etc.)
    given the manager's state and the status of the health check.
*/
envoy::admin::v3::ServerInfo::State serverState(Init::Manager::State state,
                                                bool health_check_failed);

} // namespace Utility
} // namespace Server
} // namespace Envoy
