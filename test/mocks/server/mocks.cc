#include "mocks.h"

#include "envoy/server/admin.h"
#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/header_map.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/hot_restart.h"
#include "envoy/server/options.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

using testing::_;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::ReturnRefOfCopy;
using testing::SaveArg;

namespace Server {

MockOptions::MockOptions() {}
MockOptions::~MockOptions() {}

MockAdmin::MockAdmin() {}
MockAdmin::~MockAdmin() {}

MockDrainManager::MockDrainManager() {}
MockDrainManager::~MockDrainManager() {}

MockHotRestart::MockHotRestart() {}
MockHotRestart::~MockHotRestart() {}

MockInstance::MockInstance() : ssl_context_manager_(runtime_loader_) {
  ON_CALL(*this, threadLocal()).WillByDefault(ReturnRef(thread_local_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_store_));
  ON_CALL(*this, httpTracer()).WillByDefault(ReturnRef(http_tracer_));
  ON_CALL(*this, dnsResolver()).WillByDefault(ReturnRef(dns_resolver_));
  ON_CALL(*this, api()).WillByDefault(ReturnRef(api_));
  ON_CALL(*this, admin()).WillByDefault(ReturnRef(admin_));
  ON_CALL(*this, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
  ON_CALL(*this, sslContextManager()).WillByDefault(ReturnRef(ssl_context_manager_));
  ON_CALL(*this, accessLogLock()).WillByDefault(ReturnRef(access_log_lock_));
  ON_CALL(*this, accessLogManager()).WillByDefault(ReturnRef(access_log_manager_));
  ON_CALL(*this, runtime()).WillByDefault(ReturnRef(runtime_loader_));
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
  ON_CALL(*this, hotRestart()).WillByDefault(ReturnRef(hot_restart_));
  ON_CALL(*this, random()).WillByDefault(ReturnRef(random_));
  ON_CALL(*this, getLocalAddress()).WillByDefault(ReturnRefOfCopy(std::string("127.0.0.1")));
  ON_CALL(*this, options()).WillByDefault(ReturnRef(options_));
}

MockInstance::~MockInstance() {}

} // Server
