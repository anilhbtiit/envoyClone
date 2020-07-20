#include "common/network/udp_default_writer_config.h"

#include <memory>
#include <string>

#include "envoy/config/listener/v3/udp_writer_config.pb.h"

#include "common/network/udp_packet_writer_handler_impl.h"
#include "common/network/well_known_names.h"

namespace Envoy {
namespace Network {

UdpPacketWriterPtr UdpDefaultWriterFactory::createUdpPacketWriter(Network::IoHandle& io_handle) {
  return std::make_unique<UdpDefaultWriter>(io_handle);
}

ProtobufTypes::MessagePtr UdpDefaultWriterConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::listener::v3::UdpWriterConfig>();
}

UdpPacketWriterFactoryPtr
UdpDefaultWriterConfigFactory::createUdpPacketWriterFactory(const Protobuf::Message& /*message*/) {
  return std::make_unique<UdpDefaultWriterFactory>();
}

std::string UdpDefaultWriterConfigFactory::name() const {
  return UdpWriterNames::get().DefaultWriter;
}

REGISTER_FACTORY(UdpDefaultWriterConfigFactory, Network::UdpPacketWriterConfigFactory);

} // namespace Network
} // namespace Envoy
