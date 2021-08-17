#pragma once

#include <cstdint>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/network/dns.h"

#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/network/dns_resolver/dns_factory.h"
#include "source/common/network/resolver_impl.h"

#include "absl/container/node_hash_map.h"
#include "ares.h"

namespace Envoy {
namespace Network {

class DnsResolverImplPeer;

/**
 * Implementation of DnsResolver that uses c-ares. All calls and callbacks are assumed to
 * happen on the thread that owns the creating dispatcher.
 */
class DnsResolverImpl : public DnsResolver, protected Logger::Loggable<Logger::Id::upstream> {
public:
  DnsResolverImpl(Event::Dispatcher& dispatcher,
                  const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers,
                  const envoy::config::core::v3::DnsResolverOptions& dns_resolver_options);
  ~DnsResolverImpl() override;

  // Network::DnsResolver
  ActiveDnsQuery* resolve(const std::string& dns_name, DnsLookupFamily dns_lookup_family,
                          ResolveCb callback) override;

private:
  friend class DnsResolverImplPeer;
  struct PendingResolution : public ActiveDnsQuery {
    // Network::ActiveDnsQuery
    PendingResolution(DnsResolverImpl& parent, ResolveCb callback, Event::Dispatcher& dispatcher,
                      ares_channel channel, const std::string& dns_name)
        : parent_(parent), callback_(callback), dispatcher_(dispatcher), channel_(channel),
          dns_name_(dns_name) {}

    void cancel(CancelReason) override {
      // c-ares only supports channel-wide cancellation, so we just allow the
      // network events to continue but don't invoke the callback on completion.
      // TODO(mattklein123): Potentially use timeout to destroy and recreate the channel.
      cancelled_ = true;
    }

    /**
     * ares_getaddrinfo query callback.
     * @param status return status of call to ares_getaddrinfo.
     * @param timeouts the number of times the request timed out.
     * @param addrinfo structure to store address info.
     */
    void onAresGetAddrInfoCallback(int status, int timeouts, ares_addrinfo* addrinfo);
    /**
     * wrapper function of call to ares_getaddrinfo.
     * @param family currently AF_INET and AF_INET6 are supported.
     */
    void getAddrInfo(int family);

    DnsResolverImpl& parent_;
    // Caller supplied callback to invoke on query completion or error.
    const ResolveCb callback_;
    // Dispatcher to post any callback_ exceptions to.
    Event::Dispatcher& dispatcher_;
    // Does the object own itself? Resource reclamation occurs via self-deleting
    // on query completion or error.
    bool owned_ = false;
    // Has the query completed? Only meaningful if !owned_;
    bool completed_ = false;
    // Was the query cancelled via cancel()?
    bool cancelled_ = false;
    // If dns_lookup_family is "fallback", fallback to v4 address if v6
    // resolution failed.
    bool fallback_if_failed_ = false;
    const ares_channel channel_;
    const std::string dns_name_;
  };

  struct AresOptions {
    ares_options options_;
    int optmask_;
  };

  static absl::optional<std::string>
  maybeBuildResolversCsv(const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers);

  // Callback for events on sockets tracked in events_.
  void onEventCallback(os_fd_t fd, uint32_t events);
  // c-ares callback when a socket state changes, indicating that libevent
  // should listen for read/write events.
  void onAresSocketStateChange(os_fd_t fd, int read, int write);
  // Initialize the channel.
  void initializeChannel(ares_options* options, int optmask);
  // Update timer for c-ares timeouts.
  void updateAresTimer();
  // Return default AresOptions.
  AresOptions defaultAresOptions();

  Event::Dispatcher& dispatcher_;
  Event::TimerPtr timer_;
  ares_channel channel_;
  bool dirty_channel_{};
  envoy::config::core::v3::DnsResolverOptions dns_resolver_options_;

  absl::node_hash_map<int, Event::FileEventPtr> events_;
  const absl::optional<std::string> resolvers_csv_;
};

class CaresDnsResolverFactoryImpl : public DnsResolverFactory {
public:
  std::string name() const override { return cares_dns_resolver; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig()};
  }

  DnsResolverSharedPtr createDnsResolverCb(
      Event::Dispatcher& dispatcher, const Api::Api&,
      const envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) override {
    envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
    envoy::config::core::v3::DnsResolverOptions dns_resolver_options;
    std::vector<Network::Address::InstanceConstSharedPtr> resolvers;

    // Only cares DNS factory will call into this function.
    // Directly unpack the typed config to a cares object.
    Envoy::MessageUtil::unpackTo(typed_dns_resolver_config.typed_config(), cares);
    dns_resolver_options.MergeFrom(cares.dns_resolver_options());
    if (!cares.resolvers().empty()) {
      const auto& resolver_addrs = cares.resolvers();
      resolvers.reserve(resolver_addrs.size());
      for (const auto& resolver_addr : resolver_addrs) {
        resolvers.push_back(Network::Address::resolveProtoAddress(resolver_addr));
      }
    }
    return std::make_shared<Network::DnsResolverImpl>(dispatcher, resolvers, dns_resolver_options);
  }
};

} // namespace Network
} // namespace Envoy
