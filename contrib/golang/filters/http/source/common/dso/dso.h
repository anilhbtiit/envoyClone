#pragma once

#include <dlfcn.h>

#include <string>

#include "source/common/common/logger.h"

#include "absl/synchronization/mutex.h"
#include "contrib/golang/filters/http/source/common/dso/libgolang.h"

namespace Envoy {
namespace Dso {

class DsoInstance {
public:
  DsoInstance(const std::string dso_name);
  ~DsoInstance();

  GoUint64 moeNewHttpPluginConfig(GoUint64 p0, GoUint64 p1);
  GoUint64 moeMergeHttpPluginConfig(GoUint64 p0, GoUint64 p1);

  GoUint64 moeOnHttpHeader(httpRequest* p0, GoUint64 p1, GoUint64 p2, GoUint64 p3);
  GoUint64 moeOnHttpData(httpRequest* p0, GoUint64 p1, GoUint64 p2, GoUint64 p3);

  void moeOnHttpDestroy(httpRequest* p0, int p1);

  bool loaded() { return loaded_; }

private:
  const std::string dso_name_;
  void* handler_{nullptr};
  bool loaded_{false};

  GoUint64 (*moe_new_http_plugin_config_)(GoUint64 p0, GoUint64 p1) = {nullptr};
  GoUint64 (*moe_merge_http_plugin_config_)(GoUint64 p0, GoUint64 p1) = {nullptr};

  GoUint64 (*moe_on_http_header_)(httpRequest* p0, GoUint64 p1, GoUint64 p2,
                                  GoUint64 p3) = {nullptr};
  GoUint64 (*moe_on_http_data_)(httpRequest* p0, GoUint64 p1, GoUint64 p2, GoUint64 p3) = {nullptr};

  void (*moe_on_http_destroy_)(httpRequest* p0, GoUint64 p1) = {nullptr};
};

class DsoInstanceManager {
public:
  static bool pub(std::string dso_id, std::string dso_name);
  static bool unpub(std::string dso_id);
  static DsoInstance* getDsoInstanceByID(std::string dso_id);
  static std::string show();

private:
  static absl::Mutex mutex_;
  static std::map<std::string, DsoInstance*> dso_map_;
};

} // namespace Dso
} // namespace Envoy
