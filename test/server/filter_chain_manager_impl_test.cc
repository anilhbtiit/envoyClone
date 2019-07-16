#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/init/manager.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/config/metadata.h"
#include "common/network/address_impl.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/socket_option_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"

#include "server/configuration_impl.h"
#include "server/filter_chain_manager_impl.h"
#include "server/listener_manager_impl.h"

#include "extensions/filters/listener/original_dst/original_dst.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/server/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::Throw;

namespace Envoy {
namespace Server {
class MockFilterChainFactoryBuilder : public FilterChainFactoryBuilder {
  std::unique_ptr<Network::FilterChain>
  buildFilterChain(const ::envoy::api::v2::listener::FilterChain& filter_chain) override {
    UNREFERENCED_PARAMETER(filter_chain);
    // Won't dereference but requires not nullptr.
    return std::make_unique<Network::MockFilterChain>();
  }
  // Not yet used by the this test
  void setInitManager(Init::Manager& init_manager) override {
    UNREFERENCED_PARAMETER(init_manager);
  }
};

class DependentFilterChainFactoryBuilder : public FilterChainFactoryBuilder {
public:
  std::unique_ptr<Network::FilterChain>
  buildFilterChain(const ::envoy::api::v2::listener::FilterChain& filter_chain) override {
    UNREFERENCED_PARAMETER(filter_chain);
    targets_.push_back(std::make_shared<Init::TargetImpl>("mock_builder_target", []() {}));
    init_manager_->add(*targets_.back());
    // Won't dereference but requires not nullptr.
    return std::make_unique<Network::MockFilterChain>();
  }
  // Not yet used by the this test
  void setInitManager(Init::Manager& init_manager) override { init_manager_ = &init_manager; }
  Init::Manager* init_manager_;
  std::vector<std::shared_ptr<Init::TargetImpl>> targets_;
};

class FilterChainManagerImplTest : public testing::Test {
public:
  void SetUp() override {
    local_address_ = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234);
    remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234);
    TestUtility::loadFromYaml(
        TestEnvironment::substitute(filter_chain_yaml, Network::Address::IpVersion::v4),
        filter_chain_template_);
    TestUtility::loadFromYaml(
        TestEnvironment::substitute(filter_chain_yaml_peer, Network::Address::IpVersion::v4),
        filter_chain_template_peer_);
    init_manager_ =
        std::make_unique<Init::ManagerImpl>("filter_chain_manager_init_manager_in_test");

    filter_chain_manager_ = std::make_unique<FilterChainManagerImpl>(
        *init_manager_, std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234));
    init_watcher_ = std::make_unique<Init::WatcherImpl>("filter_chain_manager_watcher", []() {
      ENVOY_LOG_MISC(warn, "filter chain manager initialized.");
    });
    init_manager_->initialize(*init_watcher_);
  }

  const Network::FilterChain*
  findFilterChainHelper(uint16_t destination_port, const std::string& destination_address,
                        const std::string& server_name, const std::string& transport_protocol,
                        const std::vector<std::string>& application_protocols,
                        const std::string& source_address, uint16_t source_port) {
    auto mock_socket = std::make_shared<NiceMock<Network::MockConnectionSocket>>();
    sockets_.push_back(mock_socket);

    if (absl::StartsWith(destination_address, "/")) {
      local_address_ = std::make_shared<Network::Address::PipeInstance>(destination_address);
    } else {
      local_address_ =
          Network::Utility::parseInternetAddress(destination_address, destination_port);
    }
    ON_CALL(*mock_socket, localAddress()).WillByDefault(ReturnRef(local_address_));

    ON_CALL(*mock_socket, requestedServerName())
        .WillByDefault(Return(absl::string_view(server_name)));
    ON_CALL(*mock_socket, detectedTransportProtocol())
        .WillByDefault(Return(absl::string_view(transport_protocol)));
    ON_CALL(*mock_socket, requestedApplicationProtocols())
        .WillByDefault(ReturnRef(application_protocols));

    if (absl::StartsWith(source_address, "/")) {
      remote_address_ = std::make_shared<Network::Address::PipeInstance>(source_address);
    } else {
      remote_address_ = Network::Utility::parseInternetAddress(source_address, source_port);
    }
    ON_CALL(*mock_socket, remoteAddress()).WillByDefault(ReturnRef(remote_address_));
    return filter_chain_manager_->findFilterChain(*mock_socket);
  }

  void addSingleFilterChainHelper(const envoy::api::v2::listener::FilterChain& filter_chain) {
    filter_chain_manager_->addFilterChain(
        std::vector<const envoy::api::v2::listener::FilterChain*>{&filter_chain},
        std::make_unique<MockFilterChainFactoryBuilder>());
  }

  // Use the returned ptr with caution. It is controlled by unique_ptr.
  DependentFilterChainFactoryBuilder* addDependentSingleFilterChainAndReturnBuilder(
      const envoy::api::v2::listener::FilterChain& filter_chain) {
    auto ptr = std::make_unique<DependentFilterChainFactoryBuilder>();
    DependentFilterChainFactoryBuilder* res = ptr.get();
    filter_chain_manager_->addFilterChain(
        std::vector<const envoy::api::v2::listener::FilterChain*>{&filter_chain}, std::move(ptr));
    return res;
  }

  // Intermedia states.
  Network::Address::InstanceConstSharedPtr local_address_;
  Network::Address::InstanceConstSharedPtr remote_address_;
  std::vector<std::shared_ptr<Network::MockConnectionSocket>> sockets_;

  // Reuseable template.
  const std::string filter_chain_yaml = R"EOF(
      filter_chain_match:
        destination_port: 10000
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_key.pem" }
        session_ticket_keys:
          keys:
          - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
  )EOF";
  const std::string filter_chain_yaml_peer = R"EOF(
      filter_chain_match:
        destination_port: 10001
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_key.pem" }
        session_ticket_keys:
          keys:
          - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
  )EOF";
  envoy::api::v2::listener::FilterChain filter_chain_template_;
  envoy::api::v2::listener::FilterChain filter_chain_template_peer_;
  std::unique_ptr<Init::Manager> init_manager_;
  std::unique_ptr<Init::Watcher> init_watcher_;

  // Test target
  std::unique_ptr<FilterChainManagerImpl> filter_chain_manager_;
};

TEST_F(FilterChainManagerImplTest, FilterChainMatchNothing) {
  auto filter_chain = findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain, nullptr);
}

TEST_F(FilterChainManagerImplTest, AddSingleFilterChain) {
  addSingleFilterChainHelper(filter_chain_template_);
  auto* filter_chain_10000 =
      findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_NE(filter_chain_10000, nullptr);
  auto* filter_chain_10001 =
      findFilterChainHelper(10001, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain_10001, nullptr);
}

TEST_F(FilterChainManagerImplTest, OverrideSingleFilterChain) {
  addSingleFilterChainHelper(filter_chain_template_);
  auto* filter_chain_10000 =
      findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_NE(filter_chain_10000, nullptr);
  auto* filter_chain_10001 =
      findFilterChainHelper(10001, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain_10001, nullptr);
  // replace port 10000 by 10001
  addSingleFilterChainHelper(filter_chain_template_peer_);
  filter_chain_10000 = findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  filter_chain_10001 = findFilterChainHelper(10001, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_NE(filter_chain_10001, nullptr);
  EXPECT_EQ(filter_chain_10000, nullptr);
}

TEST_F(FilterChainManagerImplTest, FilterChainNotAvailableWhenBeforeInitialization) {
  addDependentSingleFilterChainAndReturnBuilder(filter_chain_template_);
  auto* filter_chain_10000 =
      findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain_10000, nullptr);
}

TEST_F(FilterChainManagerImplTest, FilterChainIsAvailableWhenAfterInitialization) {
  auto builder = addDependentSingleFilterChainAndReturnBuilder(filter_chain_template_);
  auto* filter_chain_10000 =
      findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain_10000, nullptr);
  EXPECT_FALSE(builder->targets_.empty());
  for (const auto& target_ptr : builder->targets_) {
    target_ptr->ready();
  }
  filter_chain_10000 = findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_NE(filter_chain_10000, nullptr);
}

TEST_F(FilterChainManagerImplTest, FilterChainOverrideDuringInitialization) {
  auto builder = addDependentSingleFilterChainAndReturnBuilder(filter_chain_template_);
  auto* filter_chain_10000 =
      findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  auto* filter_chain_10001 =
      findFilterChainHelper(10001, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  // Neither can be addressed before initialization.
  EXPECT_FALSE(builder->targets_.empty());
  EXPECT_EQ(filter_chain_10000, nullptr);
  EXPECT_EQ(filter_chain_10001, nullptr);

  auto builder_peer = addDependentSingleFilterChainAndReturnBuilder(filter_chain_template_peer_);
  filter_chain_10000 = findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  filter_chain_10001 = findFilterChainHelper(10001, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_FALSE(builder_peer->targets_.empty());
  EXPECT_EQ(filter_chain_10000, nullptr);
  EXPECT_EQ(filter_chain_10001, nullptr);

  // Mark filter chain as ready and redo find.
  for (const auto& target_ptr : builder_peer->targets_) {
    target_ptr->ready();
  }
  filter_chain_10000 = findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  filter_chain_10001 = findFilterChainHelper(10001, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain_10000, nullptr);
  EXPECT_NE(filter_chain_10001, nullptr);
}

} // namespace Server
} // namespace Envoy