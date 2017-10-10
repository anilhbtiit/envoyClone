#pragma once

#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/network/resolver.h"

#include "common/network/address_impl.h"

#include "api/address.pb.h"

namespace Envoy {
namespace Network {
namespace Address {
/**
 * Create an Instance from a envoy::api::v2::Address.
 * @param address message.
 * @return pointer to the Instance.
 */
Address::InstanceConstSharedPtr resolveProtoAddress(const envoy::api::v2::Address& address);

/**
 * Create an Instance from a envoy::api::v2::SocketAddress.
 * @param socket address message.
 * @return pointer to the Instance.
 */
Address::InstanceConstSharedPtr
resolveProtoSocketAddress(const envoy::api::v2::SocketAddress& address);
} // namespace Address
} // namespace Network
} // namespace Envoy
