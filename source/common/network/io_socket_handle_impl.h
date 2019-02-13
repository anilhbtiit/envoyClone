#pragma once

#include "envoy/api/os_sys_calls.h"
#include "envoy/network/io_handle.h"

namespace Envoy {
namespace Network {

class IoSocketError : public IoError {
public:
  IoSocketError(int errno) : errno_(errno) {}

  ~IoSocketError() override {}

  IoErrorCode getErrorCode() const override;

  std::string getErrorDetails() const override;

private:
  int errno_;
};

/**
 * IoHandle derivative for sockets
 */
class IoSocketHandleImpl : public IoHandle {
public:
  explicit IoSocketHandleImpl(int fd = -1) : fd_(fd) {}

  // Close underlying socket if close() hasn't been call yet.
  ~IoSocketHandleImpl() override;

  // TODO(sbelair2)  To be removed when the fd is fully abstracted from clients.
  int fd() const override { return fd_; }

  IoHandleCallIntResult close() override;

  bool isOpen() const override;

private:
  int fd_;
};

} // namespace Network
} // namespace Envoy
