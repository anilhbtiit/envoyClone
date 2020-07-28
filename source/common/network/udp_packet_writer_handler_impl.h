#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/socket.h"
#include "envoy/network/udp_packet_writer_handler.h"

#include "common/network/io_socket_error_impl.h"

namespace Envoy {
namespace Network {

#define UDP_DEFAULT_WRITER_STATS(COUNTER) COUNTER(total_bytes_sent)

/**
 * Wrapper struct for udp default writer stats. @see stats_macros.h
 */
struct UdpDefaultWriterStats {
  UDP_DEFAULT_WRITER_STATS(GENERATE_COUNTER_STRUCT)
};

class UdpDefaultWriter : public UdpPacketWriter {
public:
  UdpDefaultWriter(Network::IoHandle& io_handle, Stats::Scope& scope);

  ~UdpDefaultWriter() override;

  // Following writePacket utilizes Utility::writeToSocket() implementation
  Api::IoCallUint64Result writePacket(const Buffer::Instance& buffer, const Address::Ip* local_ip,
                                      const Address::Instance& peer_address) override;

  bool isWriteBlocked() const override { return write_blocked_; }
  void setWritable() override { write_blocked_ = false; }
  uint64_t getMaxPacketSize(const Address::Instance& /*peer_address*/) const override {
    return Network::K_MAX_OUTGOING_PACKET_SIZE;
  }
  bool isBatchMode() const override { return false; }
  Network::UdpPacketWriterBuffer
  getNextWriteLocation(const Address::Ip* /*local_ip*/,
                       const Address::Instance& /*peer_address*/) override {
    return {nullptr, nullptr};
  }
  Api::IoCallUint64Result flush() override {
    return Api::IoCallUint64Result(
        /*rc=*/0,
        /*err=*/Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError));
  }

  Network::UdpDefaultWriterStats generateStats(Stats::Scope& scope);

private:
  bool write_blocked_;
  Network::IoHandle& io_handle_;
  Network::UdpDefaultWriterStats stats_;
};

} // namespace Network
} // namespace Envoy
