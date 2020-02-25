#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "envoy/config/listener/v3alpha/listener_components.pb.h"
#include "envoy/network/drain_decision.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/logger.h"
#include "common/init/manager_impl.h"
#include "common/network/cidr_range.h"
#include "common/network/lc_trie.h"

#include "server/filter_chain_factory_context_callback.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Server {

class FilterChainFactoryBuilder {
public:
  virtual ~FilterChainFactoryBuilder() = default;

  /**
   * @return Shared filter chain where builder is allowed to determine and reuse duplicated filter
   * chain. Throw exception if failed.
   */
  virtual std::shared_ptr<Network::FilterChain>
  buildFilterChain(const envoy::config::listener::v3alpha::FilterChain& filter_chain,
                   FilterChainFactoryContextCreator& context_creator) const PURE;
};

// FilterChainFactoryContextImpl is supposed to be used by network filter chain.
// Its lifetime must cover the created network filter chain.
// Its lifetime should be covered by the owned listeners so as to support replacing the active
// filter chains in the listener.
class FilterChainFactoryContextImpl : public Configuration::FilterChainFactoryContext,
                                      public Network::DrainDecision {
public:
  explicit FilterChainFactoryContextImpl(Configuration::FactoryContext& parent_context);

  // DrainDecision
  bool drainClose() const override;

  // Configuration::FactoryContext
  AccessLog::AccessLogManager& accessLogManager() override;
  Upstream::ClusterManager& clusterManager() override;
  Event::Dispatcher& dispatcher() override;
  Network::DrainDecision& drainDecision() override;
  Grpc::Context& grpcContext() override;
  bool healthCheckFailed() override;
  Tracing::HttpTracer& httpTracer() override;
  Http::Context& httpContext() override;
  Init::Manager& initManager() override;
  const LocalInfo::LocalInfo& localInfo() const override;
  Envoy::Runtime::RandomGenerator& random() override;
  Envoy::Runtime::Loader& runtime() override;
  Stats::Scope& scope() override;
  Singleton::Manager& singletonManager() override;
  OverloadManager& overloadManager() override;
  ThreadLocal::SlotAllocator& threadLocal() override;
  Admin& admin() override;
  const envoy::config::core::v3alpha::Metadata& listenerMetadata() const override;
  envoy::config::core::v3alpha::TrafficDirection direction() const override;
  TimeSource& timeSource() override;
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override;
  Api::Api& api() override;
  ServerLifecycleNotifier& lifecycleNotifier() override;
  OptProcessContextRef processContext() override;
  Configuration::ServerFactoryContext& getServerFactoryContext() const override;
  Stats::Scope& listenerScope() override;

  void setDraining() { is_draining_.store(true, std::memory_order_release); }

private:
  Configuration::FactoryContext& parent_context_;
  // atomic_flag is guaranteed to be lock free but there is no light weight load().
  std::atomic<bool> is_draining_{false};
};

class FilterChainImpl : public Network::FilterChain {
public:
  FilterChainImpl(Network::TransportSocketFactoryPtr&& transport_socket_factory,
                  std::vector<Network::FilterFactoryCb>&& filters_factory)
      : transport_socket_factory_(std::move(transport_socket_factory)),
        filters_factory_(std::move(filters_factory)) {}

  // Network::FilterChain
  const Network::TransportSocketFactory& transportSocketFactory() const override {
    return *transport_socket_factory_;
  }

  const std::vector<Network::FilterFactoryCb>& networkFilterFactories() const override {
    return filters_factory_;
  }

  void setDrainClose() { factory_context_->setDraining(); }
  // TODO(lambdai): reconsider the ownership of `factory_context_`
  std::unique_ptr<FilterChainFactoryContextImpl> factory_context_;

private:
  const Network::TransportSocketFactoryPtr transport_socket_factory_;
  const std::vector<Network::FilterFactoryCb> filters_factory_;
};

/**
 * Implementation of FilterChainManager.
 */
class FilterChainManagerImpl : public Network::FilterChainManager,
                               public FilterChainFactoryContextCreator,
                               Logger::Loggable<Logger::Id::config> {
public:
  using FcContextMap =
      std::unordered_map<envoy::config::listener::v3alpha::FilterChain,
                         std::shared_ptr<FilterChainImpl>, MessageUtil, MessageUtil>;
  FilterChainManagerImpl(const Network::Address::InstanceConstSharedPtr& address,
                         Configuration::FactoryContext& factory_context)
      : address_(address), parent_context_(factory_context) {}

  FilterChainManagerImpl(const Network::Address::InstanceConstSharedPtr& address,
                         Configuration::FactoryContext& factory_context,
                         const FilterChainManagerImpl& parent_manager);

  // FilterChainFactoryContextCreator
  std::unique_ptr<Configuration::FilterChainFactoryContext> createFilterChainFactoryContext(
      const ::envoy::config::listener::v3alpha::FilterChain* const filter_chain) override;

  // Network::FilterChainManager
  const Network::FilterChain*
  findFilterChain(const Network::ConnectionSocket& socket) const override;

  void addFilterChain(
      absl::Span<const envoy::config::listener::v3alpha::FilterChain* const> filter_chain_span,
      FilterChainFactoryBuilder& b, FilterChainFactoryContextCreator& context_creator);
  static bool isWildcardServerName(const std::string& name);

  // TODO(lambdai): move to private
  // Mapping from filter chain message to filter chain. This is used by LDS response handler to
  // detect the filter chains in the intersection of existing listener and new listener.
  FcContextMap fc_contexts_;

private:
  void convertIPsToTries();
  using SourcePortsMap = absl::flat_hash_map<uint16_t, Network::FilterChainSharedPtr>;
  using SourcePortsMapSharedPtr = std::shared_ptr<SourcePortsMap>;
  using SourceIPsMap = absl::flat_hash_map<std::string, SourcePortsMapSharedPtr>;
  using SourceIPsTrie = Network::LcTrie::LcTrie<SourcePortsMapSharedPtr>;
  using SourceIPsTriePtr = std::unique_ptr<SourceIPsTrie>;
  using SourceTypesArray = std::array<std::pair<SourceIPsMap, SourceIPsTriePtr>, 3>;
  using ApplicationProtocolsMap = absl::flat_hash_map<std::string, SourceTypesArray>;
  using TransportProtocolsMap = absl::flat_hash_map<std::string, ApplicationProtocolsMap>;
  // Both exact server names and wildcard domains are part of the same map, in which wildcard
  // domains are prefixed with "." (i.e. ".example.com" for "*.example.com") to differentiate
  // between exact and wildcard entries.
  using ServerNamesMap = absl::flat_hash_map<std::string, TransportProtocolsMap>;
  using ServerNamesMapSharedPtr = std::shared_ptr<ServerNamesMap>;
  using DestinationIPsMap = absl::flat_hash_map<std::string, ServerNamesMapSharedPtr>;
  using DestinationIPsTrie = Network::LcTrie::LcTrie<ServerNamesMapSharedPtr>;
  using DestinationIPsTriePtr = std::unique_ptr<DestinationIPsTrie>;
  using DestinationPortsMap =
      absl::flat_hash_map<uint16_t, std::pair<DestinationIPsMap, DestinationIPsTriePtr>>;

  void addFilterChainForDestinationPorts(
      DestinationPortsMap& destination_ports_map, uint16_t destination_port,
      const std::vector<std::string>& destination_ips,
      const absl::Span<const std::string* const> server_names,
      const std::string& transport_protocol,
      const absl::Span<const std::string* const> application_protocols,
      const envoy::config::listener::v3alpha::FilterChainMatch::ConnectionSourceType source_type,
      const std::vector<std::string>& source_ips,
      const absl::Span<const Protobuf::uint32> source_ports,
      const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForDestinationIPs(
      DestinationIPsMap& destination_ips_map, const std::vector<std::string>& destination_ips,
      const absl::Span<const std::string* const> server_names,
      const std::string& transport_protocol,
      const absl::Span<const std::string* const> application_protocols,
      const envoy::config::listener::v3alpha::FilterChainMatch::ConnectionSourceType source_type,
      const std::vector<std::string>& source_ips,
      const absl::Span<const Protobuf::uint32> source_ports,
      const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForServerNames(
      ServerNamesMapSharedPtr& server_names_map_ptr,
      const absl::Span<const std::string* const> server_names,
      const std::string& transport_protocol,
      const absl::Span<const std::string* const> application_protocols,
      const envoy::config::listener::v3alpha::FilterChainMatch::ConnectionSourceType source_type,
      const std::vector<std::string>& source_ips,
      const absl::Span<const Protobuf::uint32> source_ports,
      const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForApplicationProtocols(
      ApplicationProtocolsMap& application_protocol_map,
      const absl::Span<const std::string* const> application_protocols,
      const envoy::config::listener::v3alpha::FilterChainMatch::ConnectionSourceType source_type,
      const std::vector<std::string>& source_ips,
      const absl::Span<const Protobuf::uint32> source_ports,
      const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForSourceTypes(
      SourceTypesArray& source_types_array,
      const envoy::config::listener::v3alpha::FilterChainMatch::ConnectionSourceType source_type,
      const std::vector<std::string>& source_ips,
      const absl::Span<const Protobuf::uint32> source_ports,
      const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForSourceIPs(SourceIPsMap& source_ips_map, const std::string& source_ip,
                                  const absl::Span<const Protobuf::uint32> source_ports,
                                  const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForSourcePorts(SourcePortsMapSharedPtr& source_ports_map_ptr,
                                    uint32_t source_port,
                                    const Network::FilterChainSharedPtr& filter_chain);

  const Network::FilterChain*
  findFilterChainForDestinationIP(const DestinationIPsTrie& destination_ips_trie,
                                  const Network::ConnectionSocket& socket) const;
  const Network::FilterChain*
  findFilterChainForServerName(const ServerNamesMap& server_names_map,
                               const Network::ConnectionSocket& socket) const;
  const Network::FilterChain*
  findFilterChainForTransportProtocol(const TransportProtocolsMap& transport_protocols_map,
                                      const Network::ConnectionSocket& socket) const;
  const Network::FilterChain*
  findFilterChainForApplicationProtocols(const ApplicationProtocolsMap& application_protocols_map,
                                         const Network::ConnectionSocket& socket) const;
  const Network::FilterChain*
  findFilterChainForSourceTypes(const SourceTypesArray& source_types,
                                const Network::ConnectionSocket& socket) const;

  const Network::FilterChain*
  findFilterChainForSourceIpAndPort(const SourceIPsTrie& source_ips_trie,
                                    const Network::ConnectionSocket& socket) const;

  // Duplicate the inheritent factory context if any.
  std::shared_ptr<FilterChainImpl> findExistingFilterChain(
      const envoy::config::listener::v3alpha::FilterChain& filter_chain_message);

  // Mapping of FilterChain's configured destination ports, IPs, server names, transport protocols
  // and application protocols, using structures defined above.
  DestinationPortsMap destination_ports_map_;
  const Network::Address::InstanceConstSharedPtr address_;
  // This is the reference to a factory context where all the generations of listener share.
  Configuration::FactoryContext& parent_context_;
  std::list<std::shared_ptr<Configuration::FilterChainFactoryContext>> factory_contexts_;

  // Reference to the previous generation of filter chain manager. *this need to copy a subset from
  // the origin fc_contexts_ Caution: origin_ is not legit all the time.
  // TODO(lambdai): safer usage
  const FilterChainManagerImpl* origin_{};
};
} // namespace Server
} // namespace Envoy
