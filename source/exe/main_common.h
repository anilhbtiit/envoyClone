#pragma once

#include <future>

#include "common/stats/thread_local_store.h"
#include "common/thread_local/thread_local_impl.h"

#include "server/options_impl.h"
#include "server/server.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "exe/signal_action.h"
#include "exe/terminate_handler.h"
#endif

namespace Envoy {

class ProdComponentFactory : public Server::ComponentFactory {
public:
  // Server::DrainManagerFactory
  Server::DrainManagerPtr createDrainManager(Server::Instance& server) override;
  Runtime::LoaderPtr createRuntime(Server::Instance& server,
                                   Server::Configuration::Initial& config) override;
};

struct AdminResponse {
  std::unique_ptr<Http::HeaderMap> headers;
  std::string body;
};

class MainCommonBase {
public:
  MainCommonBase(OptionsImpl& options);
  ~MainCommonBase();

  bool run();

  // Makes an admin-console request by path. Returns a future that can be used
  // to access the response once ready.
  //
  // This is designed to be called from downstream consoles, so they can access
  // the admin console information stream without opening up a network port.
  //
  // This should only be called while run() is active; ensuring this is the
  // responsibility of the caller.
  std::future<AdminResponse> adminRequest(absl::string_view path_and_query,
                                          absl::string_view method);

protected:
  Envoy::OptionsImpl& options_;
  ProdComponentFactory component_factory_;
  DefaultTestHooks default_test_hooks_;
  std::unique_ptr<ThreadLocal::InstanceImpl> tls_;
  std::unique_ptr<Server::HotRestart> restarter_;
  std::unique_ptr<Stats::ThreadLocalStoreImpl> stats_store_;
  std::unique_ptr<Server::InstanceImpl> server_;
};

// TODO(jmarantz): consider removing this class; I think it'd be more useful to
// go through MainCommonBase directly.
class MainCommon {
public:
  MainCommon(int argc, const char* const* argv);
  bool run() { return base_.run(); }

  // Makes an admin-console request by path. Returns a future that can be used
  // to access the response once ready.
  //
  // This is designed to be called from downstream consoles, so they can access
  // the admin console information stream without opening up a network port.
  //
  // This should only be called while run() is active; ensuring this is the
  // responsibility of the caller.
  std::future<AdminResponse> adminRequest(absl::string_view path_and_query,
                                          absl::string_view method) {
    return base_.adminRequest(path_and_query, method);
  }

  static std::string hotRestartVersion(uint64_t max_num_stats, uint64_t max_stat_name_len,
                                       bool hot_restart_enabled);

private:
#ifdef ENVOY_HANDLE_SIGNALS
  Envoy::SignalAction handle_sigs;
  Envoy::TerminateHandler log_on_terminate;
#endif

  Envoy::OptionsImpl options_;
  MainCommonBase base_;
};

/**
 * This is the real main body that executes after site-specific
 * main() runs.
 *
 * @param options Options object initialized by site-specific code
 * @return int Return code that should be returned from the actual main()
 */
int main_common(OptionsImpl& options);

} // namespace Envoy
