#include "common/network/resolver_impl.h"

#include "envoy/common/exception.h"
#include "envoy/network/address.h"
#include "envoy/network/resolver.h"
#include "envoy/registry/registry.h"

#include "common/config/well_known_names.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Network {
namespace Address {

/**
 * Implementation of a resolver for IP addresses
 */
class IpResolver : public Resolver {

public:
  Network::Address::InstanceConstSharedPtr resolve(const std::string& address,
                                                   const uint32_t port) override {
    return Network::Utility::parseInternetAddress(address, port);
  }

  Network::Address::InstanceConstSharedPtr resolve(const std::string&,
                                                   const std::string&) override {
    throw EnvoyException("named ports are not supported by this resolver");
  }
};

/**
 * Implementation of an IP address resolver factory
 */
class IpResolverFactory : public ResolverFactory {
public:
  ResolverPtr create() const override { return ResolverPtr{new IpResolver()}; }

  std::string name() override { return Config::AddressResolverNames::get().IP; }
};

/**
 * Static registration for the IP resolver. @see RegisterFactory.
 */
static Registry::RegisterFactory<IpResolverFactory, ResolverFactory> ip_registered_;

InstanceConstSharedPtr resolveProtoAddress(const envoy::api::v2::Address& address) {
  switch (address.address_case()) {
  case envoy::api::v2::Address::kSocketAddress:
    return resolveProtoSocketAddress(address.socket_address());
  case envoy::api::v2::Address::kPipe:
    return InstanceConstSharedPtr{new PipeInstance(address.pipe().path())};
  default:
    throw EnvoyException("Address must be a socket or pipe: " + address.DebugString());
  }
}

InstanceConstSharedPtr
resolveProtoSocketAddress(const envoy::api::v2::SocketAddress& socket_address) {
  const ResolverFactory* resolver_factory = nullptr;
  const std::string& resolver_name = socket_address.resolver_name();
  if (resolver_name.empty()) {
    resolver_factory = Registry::FactoryRegistry<ResolverFactory>::getFactory(
        Config::AddressResolverNames::get().IP);
  } else {
    resolver_factory = Registry::FactoryRegistry<ResolverFactory>::getFactory(resolver_name);
  }
  if (resolver_factory == nullptr) {
    throw EnvoyException(fmt::format("Unknown address resolver: {}", resolver_name));
  }
  ResolverPtr resolver(resolver_factory->create());
  switch (socket_address.port_specifier_case()) {
  case envoy::api::v2::SocketAddress::kNamedPort:
    return resolver->resolve(socket_address.address(), socket_address.named_port());

  case envoy::api::v2::SocketAddress::kPortValue:
  // default to port 0 if no port value is specified
  case envoy::api::v2::SocketAddress::PORT_SPECIFIER_NOT_SET:
    return resolver->resolve(socket_address.address(), socket_address.port_value());

  default:
    throw EnvoyException(
        fmt::format("Unknown port specifier type {}", socket_address.port_specifier_case()));
  }
}

} // namespace Address
} // namespace Network
} // namespace Envoy
