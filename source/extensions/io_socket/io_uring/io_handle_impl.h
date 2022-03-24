#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/io_handle.h"

#include "source/common/common/logger.h"

namespace Envoy {

namespace Io {
class IoUringFactory;
} // namespace Io

namespace Extensions {
namespace IoSocket {
namespace IoUring {

class IoUringSocketHandleImpl;

enum class RequestType { Accept, Connect, Read, Write, Close, Unknown };

using IoUringSocketHandleImplOptRef =
    absl::optional<std::reference_wrapper<IoUringSocketHandleImpl>>;

struct Request {
  IoUringSocketHandleImplOptRef iohandle_{absl::nullopt};
  RequestType type_{RequestType::Unknown};
  struct iovec* iov_{nullptr};
  std::list<Buffer::SliceDataPtr> slices_{};
};

/**
 * IoHandle derivative for sockets.
 */
class IoUringSocketHandleImpl final : public Network::IoHandle,
                                      protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringSocketHandleImpl(const uint32_t read_buffer_size, const Io::IoUringFactory&,
                          os_fd_t fd = INVALID_SOCKET, bool socket_v6only = false,
                          absl::optional<int> domain = absl::nullopt);
  ~IoUringSocketHandleImpl() override;

  // Network::IoHandle
  // TODO(rojkov)  To be removed when the fd is fully abstracted from clients.
  os_fd_t fdDoNotUse() const override { return fd_; }
  Api::IoCallUint64Result close() override;
  bool isOpen() const override;
  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;
  Api::IoCallUint64Result read(Buffer::Instance& buffer,
                               absl::optional<uint64_t> max_length_opt) override;
  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;
  Api::IoCallUint64Result write(Buffer::Instance& buffer) override;
  Api::IoCallUint64Result sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice, int flags,
                                  const Network::Address::Ip* self_ip,
                                  const Network::Address::Instance& peer_address) override;
  Api::IoCallUint64Result recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice,
                                  uint32_t self_port, RecvMsgOutput& output) override;
  Api::IoCallUint64Result recvmmsg(RawSliceArrays& slices, uint32_t self_port,
                                   RecvMsgOutput& output) override;
  Api::IoCallUint64Result recv(void* buffer, size_t length, int flags) override;
  bool supportsMmsg() const override;
  bool supportsUdpGro() const override;
  Api::SysCallIntResult bind(Network::Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult listen(int backlog) override;
  Network::IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;
  Api::SysCallIntResult connect(Network::Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult setOption(int level, int optname, const void* optval,
                                  socklen_t optlen) override;
  Api::SysCallIntResult getOption(int level, int optname, void* optval, socklen_t* optlen) override;
  Api::SysCallIntResult ioctl(unsigned long, void*, unsigned long, void*, unsigned long,
                              unsigned long*) override;
  Api::SysCallIntResult setBlocking(bool blocking) override;
  absl::optional<int> domain() override;
  Network::Address::InstanceConstSharedPtr localAddress() override;
  Network::Address::InstanceConstSharedPtr peerAddress() override;
  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override;
  Network::IoHandlePtr duplicate() override;
  void activateFileEvents(uint32_t events) override;
  void enableFileEvents(uint32_t events) override;
  void resetFileEvents() override;
  Api::SysCallIntResult shutdown(int how) override;
  absl::optional<std::chrono::milliseconds> lastRoundTripTime() override { return absl::nullopt; }
  absl::optional<uint64_t> congestionWindowInBytes() const override { return absl::nullopt; }
  absl::optional<std::string> interfaceName() override;

private:
  // FileEventAdapter adapts `io_uring` to libevent.
  class FileEventAdapter {
  public:
    FileEventAdapter(const uint32_t read_buffer_size, const Io::IoUringFactory& io_uring_factory,
                     os_fd_t fd)
        : read_buffer_size_(read_buffer_size), io_uring_factory_(io_uring_factory), fd_(fd) {}
    void initialize(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                    Event::FileTriggerType trigger, uint32_t events);
    Network::IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen);
    void addAcceptRequest();

  private:
    void onFileEvent();
    void onRequestCompletion(const Request& req, int32_t result);

    const uint32_t read_buffer_size_;
    const Io::IoUringFactory& io_uring_factory_;
    os_fd_t fd_;
    Event::FileReadyCb cb_;
    Event::FileEventPtr file_event_{nullptr};
    os_fd_t connection_fd_{INVALID_SOCKET};
    bool is_accept_added_{false};
    struct sockaddr remote_addr_;
    socklen_t remote_addr_len_{sizeof(remote_addr_)};
  };

  void addReadRequest();
  // Checks if the io handle is the one that registered eventfd with `io_uring`.
  // An io handle can be a leader in two cases:
  //   1. it's a server socket accepting new connections;
  //   2. it's a client socket about to connect to a remote socket, but created
  //      in a thread without properly initialized `io_uring`.
  bool isLeader() const { return file_event_adapter_ != nullptr; }

  const uint32_t read_buffer_size_;
  const Io::IoUringFactory& io_uring_factory_;
  os_fd_t fd_;
  int socket_v6only_;
  const absl::optional<int> domain_;

  Event::FileReadyCb cb_;
  struct iovec iov_;
  std::unique_ptr<uint8_t[]> read_buf_{nullptr};
  int32_t bytes_to_read_{0};
  bool is_read_added_{false};
  bool is_read_enabled_{true};
  std::unique_ptr<FileEventAdapter> file_event_adapter_{nullptr};
};

} // namespace IoUring
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
