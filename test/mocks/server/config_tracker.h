#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

#include "envoy/common/mutex_tracer.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/admin.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/server/configuration.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/health_checker_config.h"
#include "envoy/server/instance.h"
#include "envoy/server/options.h"
#include "envoy/server/overload_manager.h"
#include "envoy/server/tracer_config.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/server/worker.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread/thread.h"

#include "common/grpc/context_impl.h"
#include "common/http/context_impl.h"
#include "common/secret/secret_manager_impl.h"
#include "common/stats/fake_symbol_table_impl.h"

#include "extensions/transport_sockets/tls/context_manager_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/test_time_system.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {
class MockConfigTracker : public ConfigTracker {
public:
  MockConfigTracker();
  ~MockConfigTracker() override;

  struct MockEntryOwner : public EntryOwner {};

  MOCK_METHOD(EntryOwner*, add_, (std::string, Cb));

  // Server::ConfigTracker
  MOCK_METHOD(const CbsMap&, getCallbacksMap, (), (const));
  EntryOwnerPtr add(const std::string& key, Cb callback) override {
    return EntryOwnerPtr{add_(key, std::move(callback))};
  }

  std::unordered_map<std::string, Cb> config_tracker_callbacks_;
};
} // namespace Server

} // namespace Envoy
