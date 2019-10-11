#pragma once

#include "envoy/router/router.h"

#include "test/mocks/server/mocks.h"

namespace Envoy {
class RouteCoverage : Logger::Loggable<Logger::Id::testing> {
public:
  RouteCoverage(const Envoy::Router::Route* route) : route_(*route){};

  double report();
  void setClusterCovered() { cluster_covered_ = true; }
  void setVirtualClusterCovered() { virtual_cluster_covered_ = true; }
  void setVirtualHostCovered() { virtual_host_covered_ = true; }
  void setPathRewriteCovered() { path_rewrite_covered_ = true; }
  void setHostRewriteCovered() { host_rewrite_covered_ = true; }
  void setRedirectPathCovered() { redirect_path_covered_ = true; }
  bool covers(const Envoy::Router::Route* route);
  const std::string routeName();

private:
  const Envoy::Router::Route& route_;
  bool cluster_covered_{false};
  bool virtual_cluster_covered_{false};
  bool virtual_host_covered_{false};
  bool path_rewrite_covered_{false};
  bool host_rewrite_covered_{false};
  bool redirect_path_covered_{false};

  std::vector<bool> coverageFields() {
    return std::vector<bool>{cluster_covered_,      virtual_cluster_covered_,
                             virtual_host_covered_, path_rewrite_covered_,
                             host_rewrite_covered_, redirect_path_covered_};
  }
};

class Coverage : Logger::Loggable<Logger::Id::testing> {
public:
  Coverage(envoy::api::v2::RouteConfiguration config) : route_config_(config){};
  void markClusterCovered(const Envoy::Router::Route& route);
  void markVirtualClusterCovered(const Envoy::Router::Route& route);
  void markVirtualHostCovered(const Envoy::Router::Route& route);
  void markPathRewriteCovered(const Envoy::Router::Route& route);
  void markHostRewriteCovered(const Envoy::Router::Route& route);
  void markRedirectPathCovered(const Envoy::Router::Route& route);
  double report(bool detailed);
  void printMissingTests(const std::set<std::string>& missing_route_names);

private:
  RouteCoverage& coveredRoute(const Envoy::Router::Route& route);

  std::vector<std::unique_ptr<RouteCoverage>> covered_routes_;
  const envoy::api::v2::RouteConfiguration route_config_;
};
} // namespace Envoy
