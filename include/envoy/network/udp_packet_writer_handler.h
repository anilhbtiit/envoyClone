#pragma once

#include <cstdint>
#include <memory>

#include "envoy/api/io_error.h"    // IoCallUint64Result
#include "envoy/buffer/buffer.h"   // Buffer
#include "envoy/network/address.h" // Address
#include "envoy/network/socket.h"  // Socket
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Network {

/**
 * Max v6 packet size, excluding IP and UDP headers.
 */
static const uint64_t K_MAX_OUTGOING_PACKET_SIZE = 1452;

#define UDP_PACKET_WRITER_STATS(COUNTER, GAUGE, HISTOGRAM)                                         \
  COUNTER(total_bytes_sent)                                                                        \
  GAUGE(internal_buffer_size, NeverImport)                                                         \
  GAUGE(front_buffered_pkt_size, NeverImport)                                                      \
  HISTOGRAM(pkts_sent_per_batch, Unspecified)

/**
 * Wrapper struct for udp packet writer stats. @see stats_macros.h
 */
struct UdpPacketWriterStats {
  UDP_PACKET_WRITER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

/**
 * UdpPacketWriterBuffer bundles a buffer and a function that
 * releases it.
 */
struct UdpPacketWriterBuffer {
  UdpPacketWriterBuffer() = default;
  UdpPacketWriterBuffer(char* buffer, std::function<void(const char*)> release_buffer)
      : buffer_(buffer), release_buffer_(std::move(release_buffer)) {}

  char* buffer_ = nullptr;
  std::function<void(const char*)> release_buffer_;
};

class UdpPacketWriter {
public:
  virtual ~UdpPacketWriter() = default;

  /**
   * @brief Sends a packet via given UDP socket with specific source address.
   *
   * @param buffer points to the buffer containing the packet
   * @param local_ip is the source address to be used to send.
   * @param peer_address is the destination address to send to.
   * @return result with number of bytes written, and write status
   */
  virtual Api::IoCallUint64Result writePacket(const Buffer::Instance& buffer,
                                              const Address::Ip* local_ip,
                                              const Address::Instance& peer_address) PURE;

  /**
   * @returns true if the network socket is not writable.
   */
  virtual bool isWriteBlocked() const PURE;

  /**
   * @brief mark the socket as writable when the socket is unblocked.
   */
  virtual void setWritable() PURE;

  /**
   * @brief Get the maximum size of the packet which can be written using this
   * writer for the supplied peer address.
   *
   * @param peer_address  is the destination address to send to.
   * @return the max packet size
   */
  virtual uint64_t getMaxPacketSize(const Address::Instance& peer_address) const PURE;

  /**
   * @return true if Batch Mode
   * @return false if PassThroughMode
   */
  virtual bool isBatchMode() const PURE;

  /**
   * @brief Get pointer to the next write location in internal buffer,
   * it should be called iff the caller does not call writePacket
   * for the returned buffer. The caller is expected to call writePacket
   * with the buffer returned from this function to save a memcpy.
   *
   * @param local_ip is the source address to be used to send.
   * @param peer_address is the destination address to send to.
   * @return { char* to the next write location,
   *           func to release buffer }
   */
  virtual UdpPacketWriterBuffer getNextWriteLocation(const Address::Ip* local_ip,
                                                     const Address::Instance& peer_address) PURE;

  /**
   * @brief Batch Mode: Try to send all buffered packets
   *        PassThrough Mode: NULL operation
   *
   * @return Api::IoCallUint64Result
   */
  virtual Api::IoCallUint64Result flush() PURE;
};

using UdpPacketWriterPtr = std::unique_ptr<UdpPacketWriter>;

class UdpPacketWriterFactory {
public:
  virtual ~UdpPacketWriterFactory() = default;

  /**
   * Creates an UdpPacketWriter object for the given Udp Socket
   * @param socket UDP socket used to send packets.
   * @return the UdpPacketWriter created.
   */
  virtual UdpPacketWriterPtr createUdpPacketWriter(Network::IoHandle& io_handle,
                                                   Stats::Scope& scope) PURE;
};

using UdpPacketWriterFactoryPtr = std::unique_ptr<UdpPacketWriterFactory>;

} // namespace Network
} // namespace Envoy
