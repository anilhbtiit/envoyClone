#pragma once

#include "envoy/network/address.h"

#include <sys/un.h>

namespace Network {
namespace Address {

/**
 * Base class for all address types.
 */
class InstanceBase : public Instance {
public:
  // Network::Address::Instance
  bool operator==(const Instance& rhs) const override { return asString() == rhs.asString(); }
  const std::string& asString() const override { return friendly_name_; }
  Type type() const override { return type_; }

protected:
  InstanceBase(Type type) : type_(type) {}
  int flagsFromSocketType(SocketType type) const;

  std::string friendly_name_;

private:
  const Type type_;
};

/**
 * Implementation of an IPv4 address.
 */
class Ipv4Instance : public InstanceBase {
public:
  /**
   * Construct from an existing unix IPv4 socket address (IP v4 address and port).
   */
  Ipv4Instance(const sockaddr_in* address);

  /**
   * Construct from a string IPv4 address such as "1.2.3.4". Port will be unset/0.
   */
  Ipv4Instance(const std::string& address);

  /**
   * Construct from a string IPv4 address such as "1.2.3.4" as well as a port.
   */
  Ipv4Instance(const std::string& address, uint32_t port);

  /**
   * Construct from a port. The IPv4 address will be set to "any" and is suitable for binding
   * a port to any available address.
   */
  Ipv4Instance(uint32_t port);

  // Network::Address::Instance
  int bind(int fd) const override;
  int connect(int fd) const override;
  const Ip* ip() const override { return &ip_; }
  int socket(SocketType type) const override;

private:
  struct Ipv4Helper : public Ipv4 {
    uint32_t address() const override { return address_.sin_addr.s_addr; }

    sockaddr_in address_;
  };

  struct IpHelper : public Ip {
    const std::string& addressAsString() const override { return friendly_address_; }
    const Ipv4* ipv4() const override { return &ipv4_; }
    const Ipv6* ipv6() const override { return nullptr; }
    uint32_t port() const override { return ntohs(ipv4_.address_.sin_port); }
    IpVersion version() const override { return IpVersion::v4; }

    Ipv4Helper ipv4_;
    std::string friendly_address_;
  };

  IpHelper ip_;
};

/**
 * Implementation of an IPv6 address.
 */
class Ipv6Instance : public InstanceBase {
public:
  /**
   * Construct from an existing unix IPv6 socket address (IP v6 address and port).
   */
  Ipv6Instance(const sockaddr_in6& address);

  /**
   * Construct from a string IPv6 address such as "12:34::5". Port will be unset/0.
   */
  Ipv6Instance(const std::string& address);

  /**
   * Construct from a string IPv6 address such as "12:34::5" as well as a port.
   */
  Ipv6Instance(const std::string& address, uint32_t port);

  /**
   * Construct from a port. The IPv6 address will be set to "any" and is suitable for binding
   * a port to any available address.
   */
  Ipv6Instance(uint32_t port);

  // Network::Address::Instance
  int bind(int fd) const override;
  int connect(int fd) const override;
  const Ip* ip() const override { return &ip_; }
  int socket(SocketType type) const override;

private:
  struct Ipv6Helper : public Ipv6 {
    std::array<uint8_t, 16> address() const override;
    uint32_t port() const;

    std::string makeFriendlyAddress() const;

    sockaddr_in6 address_;
  };

  struct IpHelper : public Ip {
    const std::string& addressAsString() const override { return friendly_address_; }
    const Ipv4* ipv4() const override { return nullptr; }
    const Ipv6* ipv6() const override { return &ipv6_; }
    uint32_t port() const override { return ipv6_.port(); }
    IpVersion version() const override { return IpVersion::v6; }

    Ipv6Helper ipv6_;
    std::string friendly_address_;
  };

  IpHelper ip_;
};

/**
 * Given an IP address and a length of high order bits to keep, returns an address
 * where those high order bits are unmodified, and the remaining bits are all zero.
 * length_io is reduced to be at most 32 for IPv4 address and at most 128 for IPv6
 * addresses. If the address is invalid or the length is less than zero, then *length_io
 * is set to -1 and nullptr is returned.
 * @return a pointer to an address where the high order *length_io bits are unmodified
 * from address, and *length_io is in the range 0 to N, where N is the number of bits
 * in an address of the IP version (i.e. address->ip()->version()).
 */
InstancePtr truncateIpAddressAndLength(const InstancePtr& address, int* length_io);

/**
 * Implementation of a pipe address (unix domain socket on unix).
 */
class PipeInstance : public InstanceBase {
public:
  /**
   * Construct from an existing unix address.
   */
  PipeInstance(const sockaddr_un* address);

  /**
   * Construct from a string pipe path.
   */
  PipeInstance(const std::string& pipe_path);

  // Network::Address::Instance
  int bind(int fd) const override;
  int connect(int fd) const override;
  const Ip* ip() const override { return nullptr; }
  int socket(SocketType type) const override;

private:
  sockaddr_un address_;
};

} // Address
} // Network
