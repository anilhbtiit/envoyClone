#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {

class XdsVerifier {
public:
  XdsVerifier();
  void listenerAdded(envoy::config::listener::v3::Listener listener, bool from_update = false);
  void listenerUpdated(envoy::config::listener::v3::Listener listener);
  void listenerRemoved(const std::string& name);
  void drainedListener(const std::string& name);

  void routeAdded(envoy::config::route::v3::RouteConfiguration route);
  void routeUpdated(envoy::config::route::v3::RouteConfiguration route);

  enum ListenerState { WARMING, ACTIVE, DRAINING };
  struct ListenerRepresentation {
    envoy::config::listener::v3::Listener listener;
    ListenerState state;
  };

  const std::vector<ListenerRepresentation>& listeners() const { return listeners_; }

  uint32_t numWarming() { return num_warming_; }
  uint32_t numActive() { return num_active_; }
  uint32_t numDraining() { return num_draining_; }

  uint32_t numAdded() { return num_added_; }
  uint32_t numModified() { return num_modified_; }
  uint32_t numRemoved() { return num_removed_; }

  void dumpState();

private:
  std::string getRoute(const envoy::config::listener::v3::Listener& listener);
  bool hasRoute(const envoy::config::listener::v3::Listener& listener);
  std::vector<ListenerRepresentation> listeners_;
  absl::flat_hash_map<std::string, envoy::config::route::v3::RouteConfiguration> routes_;

  uint32_t num_warming_;
  uint32_t num_active_;
  uint32_t num_draining_;

  uint32_t num_added_;
  uint32_t num_modified_;
  uint32_t num_removed_;
};

} // namespace Envoy
