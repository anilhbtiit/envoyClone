#ifndef LOCAL_GOOGLE_HOME_AUNI_ENVOY_SOURCE_EXTENSIONS_FILTERS_NETWORK_HTTP_CONNECTION_MANAGER_DEPENDENCY_MANAGER_H_
#define LOCAL_GOOGLE_HOME_AUNI_ENVOY_SOURCE_EXTENSIONS_FILTERS_NETWORK_HTTP_CONNECTION_MANAGER_DEPENDENCY_MANAGER_H_

#include "envoy/extensions/filters/common/dependency/v3/dependency.pb.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {

class DependencyManager {
public:
  DependencyManager() {}

  void RegisterFilter(std::string name, envoy::extensions::filters::common::dependency::v3::FilterDependencies dependencies) {
    dependencies_.push_back({name, dependencies});
  }

  bool IsValid() {
    for (auto& [name, dependencies] : dependencies_) {
      for (auto& requirement : dependencies.decode_required()) {
        std::cerr << "[AUNI] " << requirement.DebugString() << "\n";

        return false;
      }
      std::cerr << "[AUNI] " << name << "\n";
      std::cerr << "[AUNI] " << dependencies.DebugString() << "\n";
    }

    return true;
  }

private:
  std::vector<std::pair<std::string, envoy::extensions::filters::common::dependency::v3::FilterDependencies>> dependencies_;
};

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

#endif  // LOCAL_GOOGLE_HOME_AUNI_ENVOY_SOURCE_EXTENSIONS_FILTERS_NETWORK_HTTP_CONNECTION_MANAGER_DEPENDENCY_MANAGER_H_
