#include "source/common/network/io_uring_socket_handle_impl.h"

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/io/io_uring_worker_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface_impl.h"

namespace Envoy {
namespace Network {

IoUringSocketHandleImpl::IoUringSocketHandleImpl(Io::IoUringFactory& io_uring_factory, os_fd_t fd,
                                                 bool socket_v6only, absl::optional<int> domain,
                                                 bool is_server_socket)
    : IoSocketHandleBaseImpl(fd, socket_v6only, domain), io_uring_factory_(io_uring_factory) {
  ENVOY_LOG(trace, "construct io uring socket handle, fd = {}, is_server_socket = {}", fd_,
            is_server_socket);
  if (is_server_socket) {
    io_uring_socket_type_ = IoUringSocketType::Server;
    if (!enable_server_socket_) {
      shadow_io_handle_ = std::make_unique<IoSocketHandleImpl>(fd_, socket_v6only_, domain_);
      shadow_io_handle_->setBlocking(false);
    }
  }
}

IoUringSocketHandleImpl::~IoUringSocketHandleImpl() {
  ENVOY_LOG(trace, "~IoUringSocketHandleImpl, type = {}", ioUringSocketTypeStr());
  if (SOCKET_VALID(fd_)) {
    if (io_uring_socket_type_ != IoUringSocketType::Unknown && io_uring_socket_.has_value()) {
      io_uring_socket_.ref().close(false);
    } else {
      // The TLS slot has been shut down by this moment with IoUring wiped out, thus
      // better use this posix system call instead of IoUringSocketHandleImpl::close().
      ::close(fd_);
    }
  }
}

Api::IoCallUint64Result IoUringSocketHandleImpl::close() {
  ASSERT(SOCKET_VALID(fd_));
  ENVOY_LOG(trace, "close, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  // Fall back to shadow io handle if client/server socket disabled.
  // This will be removed when all the debug work done.
  if ((io_uring_socket_type_ == IoUringSocketType::Client && !enable_client_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Server && !enable_server_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Accept && !enable_accept_socket_)) {
    ENVOY_LOG(trace, "fallback to shadow io uring handle, fd = {}, type = {}", fd_,
              ioUringSocketTypeStr());
    if (shadow_io_handle_ == nullptr) {
      ::close(fd_);
      SET_SOCKET_INVALID(fd_);
      return Api::ioCallUint64ResultNoError();
    }
    SET_SOCKET_INVALID(fd_);
    return shadow_io_handle_->close();
  }

  if (io_uring_socket_type_ == IoUringSocketType::Unknown || !io_uring_socket_.has_value()) {
    ::close(fd_);
    SET_SOCKET_INVALID(fd_);
    return Api::ioCallUint64ResultNoError();
  }

  io_uring_socket_.ref().close(false);
  SET_SOCKET_INVALID(fd_);
  return Api::ioCallUint64ResultNoError();
}

Api::IoCallUint64Result
IoUringSocketHandleImpl::readv(uint64_t max_length, Buffer::RawSlice* slices, uint64_t num_slice) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Accept);
  ENVOY_LOG(debug, "readv, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  // Fall back to shadow io handle if client/server socket disabled.
  // This will be removed when all the debug work done.
  if ((io_uring_socket_type_ == IoUringSocketType::Client && !enable_client_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Server && !enable_server_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Accept && !enable_accept_socket_)) {
    ENVOY_LOG(debug, "readv fallback to shadow io handle, fd = {}, type = {}", fd_,
              ioUringSocketTypeStr());
    return shadow_io_handle_->readv(max_length, slices, num_slice);
  }

  Api::IoCallUint64Result result = copyOut(max_length, slices, num_slice);
  if (result.ok()) {
    // If the return is 0, this should be remote close. Return
    // the result directly.
    if (result.return_value_ != 0) {
      read_param_->buf_.drain(result.return_value_);
    }
  }
  return result;
}

Api::IoCallUint64Result IoUringSocketHandleImpl::read(Buffer::Instance& buffer,
                                                      absl::optional<uint64_t> max_length_opt) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Accept);
  ENVOY_LOG(trace, "read, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  // Fall back to shadow io handle if client/server socket disabled.
  // This will be removed when all the debug work done.
  if ((io_uring_socket_type_ == IoUringSocketType::Client && !enable_client_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Server && !enable_server_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Accept && !enable_accept_socket_)) {
    return shadow_io_handle_->read(buffer, max_length_opt);
  }

  // It means there is no io_uring request is done, the read is invoked directly.
  if (read_param_ == absl::nullopt) {
    if (io_uring_socket_->getStatus() != Io::IoUringSocketStatus::RemoteClosed) {
      return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                                 IoSocketError::deleteIoError)};
    } else {
      ENVOY_LOG(trace, "read remote close");
      return Api::ioCallUint64ResultNoError();
    }
  }

  ASSERT(io_uring_socket_.has_value());

  if (read_param_->result_ == 0) {
    ENVOY_LOG(trace, "read remote close");
    return Api::ioCallUint64ResultNoError();
  }

  if (read_param_->result_ < 0) {
    ASSERT(read_param_->buf_.length() == 0);
    ENVOY_LOG(trace, "read got error");
    if (read_param_->result_ == -EAGAIN) {
      return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                                 IoSocketError::deleteIoError)};
    }
    return {
        0, Api::IoErrorPtr(new IoSocketError(-read_param_->result_), IoSocketError::deleteIoError)};
  }

  // This mean the buffer ready read by previous call, return EAGAIN to tell the
  // caller waiting for next read event.
  if (read_param_->buf_.length() == 0) {
    return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                               IoSocketError::deleteIoError)};
  }

  uint64_t max_read_length =
      std::min(max_length_opt.value_or(UINT64_MAX), read_param_->buf_.length());
  buffer.move(read_param_->buf_, max_read_length);
  return {max_read_length, Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError)};
}

Api::IoCallUint64Result IoUringSocketHandleImpl::writev(const Buffer::RawSlice* slices,
                                                        uint64_t num_slice) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Accept);
  ENVOY_LOG(trace, "writev, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  if ((io_uring_socket_type_ == IoUringSocketType::Client && !enable_client_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Server && !enable_server_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Accept && !enable_accept_socket_)) {
    return shadow_io_handle_->writev(slices, num_slice);
  }

  if (write_param_ != absl::nullopt) {
    // EAGAIN means an injected event, then just submit new write.
    if (write_param_->result_ < 0 && write_param_->result_ != -EAGAIN) {
      return {0, Api::IoErrorPtr(new IoSocketError(write_param_->result_),
                                 IoSocketError::deleteIoError)};
    }
    ENVOY_LOG(trace, "an inject event, result = {}, fd = {}", write_param_->result_, fd_);
  }

  ASSERT(io_uring_socket_.has_value());
  auto ret = io_uring_socket_->write(slices, num_slice);
  return {ret, Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError)};
}

Api::IoCallUint64Result IoUringSocketHandleImpl::write(Buffer::Instance& buffer) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  ENVOY_LOG(trace, "write, length = {}, fd = {}, type = {}", buffer.length(), fd_,
            ioUringSocketTypeStr());

  if ((io_uring_socket_type_ == IoUringSocketType::Client && !enable_client_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Server && !enable_server_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Accept && !enable_accept_socket_)) {
    return shadow_io_handle_->write(buffer);
  }

  if (write_param_ != absl::nullopt) {
    // EAGAIN means an injected event, then just submit new write.
    if (write_param_->result_ < 0 && write_param_->result_ != -EAGAIN) {
      return {0, Api::IoErrorPtr(new IoSocketError(write_param_->result_),
                                 IoSocketError::deleteIoError)};
    }
    ENVOY_LOG(trace, "an inject event, result = {}, fd = {}", write_param_->result_, fd_);
  }

  ASSERT(io_uring_socket_.has_value());
  auto buffer_size = buffer.length();
  io_uring_socket_->write(buffer);
  return {buffer_size, Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError)};
}

Api::IoCallUint64Result IoUringSocketHandleImpl::sendmsg(const Buffer::RawSlice* slices,
                                                         uint64_t num_slice, int flags,
                                                         const Address::Ip* self_ip,
                                                         const Address::Instance& peer_address) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Accept);
  ENVOY_LOG(trace, "sendmsg, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  if ((io_uring_socket_type_ == IoUringSocketType::Client && !enable_client_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Server && !enable_server_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Accept && !enable_accept_socket_)) {
    return shadow_io_handle_->sendmsg(slices, num_slice, flags, self_ip, peer_address);
  }
  PANIC("IoUringSocketHandleImpl::sendmsg not implement");
}

Api::IoCallUint64Result IoUringSocketHandleImpl::recvmsg(Buffer::RawSlice* slices,
                                                         const uint64_t num_slice,
                                                         uint32_t self_port,
                                                         RecvMsgOutput& output) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Accept);
  ENVOY_LOG(trace, "recvmsg, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  if ((io_uring_socket_type_ == IoUringSocketType::Client && !enable_client_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Server && !enable_server_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Accept && !enable_accept_socket_)) {
    return shadow_io_handle_->recvmsg(slices, num_slice, self_port, output);
  }
  PANIC("IoUringSocketHandleImpl::recvmsg not implemented");
}

Api::IoCallUint64Result IoUringSocketHandleImpl::recvmmsg(RawSliceArrays& slices,
                                                          uint32_t self_port,
                                                          RecvMsgOutput& output) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Accept);
  ENVOY_LOG(trace, "recvmmsg, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  if ((io_uring_socket_type_ == IoUringSocketType::Client && !enable_client_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Server && !enable_server_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Accept && !enable_accept_socket_)) {
    return shadow_io_handle_->recvmmsg(slices, self_port, output);
  }
  PANIC("IoUringSocketHandleImpl::recvmmsg not implemented");
}

Api::IoCallUint64Result IoUringSocketHandleImpl::recv(void* buffer, size_t length, int flags) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Accept);
  ENVOY_LOG(trace, "recv, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  if ((io_uring_socket_type_ == IoUringSocketType::Client && !enable_client_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Server && !enable_server_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Accept && !enable_accept_socket_)) {
    return shadow_io_handle_->recv(buffer, length, flags);
  }

  // The only used valid in Envoy is MSG_PEEK for listener filter including TLS inspectors.
  ASSERT(flags == 0 || flags == MSG_PEEK);
  Buffer::RawSlice slice;
  slice.mem_ = buffer;
  slice.len_ = length;
  if (flags == 0) {
    return readv(length, &slice, 1);
  }

  return copyOut(length, &slice, 1);
}

Api::SysCallIntResult IoUringSocketHandleImpl::bind(Address::InstanceConstSharedPtr address) {
  ENVOY_LOG(trace, "bind to address {}", address->asString());
  ENVOY_LOG(trace, "bind, fd = {}, io_uring_socket_type = {}", fd_, ioUringSocketTypeStr());
  return Api::OsSysCallsSingleton::get().bind(fd_, address->sockAddr(), address->sockAddrLen());
}

Api::SysCallIntResult IoUringSocketHandleImpl::listen(int backlog) {
  ASSERT(io_uring_socket_type_ == IoUringSocketType::Unknown);
  ENVOY_LOG(trace, "listen, fd = {}, io_uring_socket_type = {}", fd_, ioUringSocketTypeStr());
  io_uring_socket_type_ = IoUringSocketType::Accept;
  if (!enable_accept_socket_) {
    ENVOY_LOG(trace, "fallback to create IoSocketHandle, fd = {}, io_uring_socket_type = {}", fd_,
              ioUringSocketTypeStr());
    shadow_io_handle_ = std::make_unique<IoSocketHandleImpl>(fd_, socket_v6only_, domain_);
    shadow_io_handle_->setBlocking(false);
    return shadow_io_handle_->listen(backlog);
  }
  return Api::OsSysCallsSingleton::get().listen(fd_, backlog);
}

IoHandlePtr IoUringSocketHandleImpl::accept(struct sockaddr* addr, socklen_t* addrlen) {
  ASSERT(io_uring_socket_type_ == IoUringSocketType::Accept);
  ENVOY_LOG(trace, "accept, fd = {}, io_uring_socket_type = {}", fd_, ioUringSocketTypeStr());

  if (!enable_accept_socket_) {
    ENVOY_LOG(trace, "fallback to IoSocketHandle for accept socket");
    auto result = Api::OsSysCallsSingleton::get().accept(fd_, addr, addrlen);
    if (SOCKET_INVALID(result.return_value_)) {
      ENVOY_LOG(trace, "accept return invalid socket");
      return nullptr;
    }
    return std::make_unique<IoUringSocketHandleImpl>(io_uring_factory_, result.return_value_,
                                                     socket_v6only_, domain_, true);
  }

  if (!accepted_socket_param_.has_value()) {
    return nullptr;
  }

  if (SOCKET_INVALID(accepted_socket_param_->fd_)) {
    ENVOY_LOG(trace, "IoUringSocketHandleImpl accept invalid socket");
    accepted_socket_param_ = absl::nullopt;

    return nullptr;
  }

  ENVOY_LOG(
      trace, "IoUringSocketHandleImpl accept the socket, connect fd = {}, remote address = {}",
      accepted_socket_param_->fd_,
      Network::Address::addressFromSockAddrOrThrow(*accepted_socket_param_->remote_addr_,
                                                   accepted_socket_param_->remote_addr_len_, false)
          ->asString());
  memcpy(reinterpret_cast<void*>(addr), // NOLINT(safe-memcpy)
         reinterpret_cast<void*>(accepted_socket_param_->remote_addr_),
         accepted_socket_param_->remote_addr_len_);
  *addrlen = accepted_socket_param_->remote_addr_len_;
  auto io_handle = std::make_unique<IoUringSocketHandleImpl>(
      io_uring_factory_, accepted_socket_param_->fd_, socket_v6only_, domain_, true);
  accepted_socket_param_ = absl::nullopt;

  return io_handle;
}

Api::SysCallIntResult IoUringSocketHandleImpl::connect(Address::InstanceConstSharedPtr address) {
  ASSERT(io_uring_socket_type_ == IoUringSocketType::Client);
  ENVOY_LOG(trace, "connect, fd = {}, io_uring_socket_type = {}", fd_, ioUringSocketTypeStr());

  if (!enable_client_socket_) {
    ENVOY_LOG(trace, "fallback to IoSocketHandle for client socket");
    return shadow_io_handle_->connect(address);
  }

  return Api::SysCallIntResult{0, 0};
}

Api::SysCallIntResult IoUringSocketHandleImpl::shutdown(int how) {
  ENVOY_LOG(trace, "shutdown, fd = {}, io_uring_socket_type = {}", fd_, ioUringSocketTypeStr());

  if ((io_uring_socket_type_ == IoUringSocketType::Client && !enable_client_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Server && !enable_server_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Accept && !enable_accept_socket_)) {
    ENVOY_LOG(trace, "fallback to IoSocketHandle for shutdown socket");
    return shadow_io_handle_->shutdown(how);
  }

  io_uring_socket_->shutdown(how);
  return Api::SysCallIntResult{0, 0};
}

void IoUringSocketHandleImpl::initializeFileEvent(Event::Dispatcher& dispatcher,
                                                  Event::FileReadyCb cb,
                                                  Event::FileTriggerType trigger, uint32_t events) {
  ENVOY_LOG(trace, "initialize file event fd = {}, io_uring_socket_type = {}, has socket = {}", fd_,
            ioUringSocketTypeStr(), io_uring_socket_.has_value());

  // The io_uring_socket_ already created. This happened after resetFileEvent;
  if (io_uring_socket_.has_value()) {
    if ((io_uring_socket_type_ == IoUringSocketType::Client && !enable_client_socket_) ||
        (io_uring_socket_type_ == IoUringSocketType::Server && !enable_server_socket_) ||
        (io_uring_socket_type_ == IoUringSocketType::Accept && !enable_accept_socket_)) {
      shadow_io_handle_->initializeFileEvent(dispatcher, std::move(cb), trigger, events);
      return;
    }

    ENVOY_LOG(trace, "compare dispatcher, {} == {}",
              io_uring_socket_->getIoUringWorker().dispatcher().name(),
              io_uring_factory_.getIoUringWorker()->dispatcher().name());
    if (&io_uring_socket_->getIoUringWorker().dispatcher() ==
        &io_uring_factory_.getIoUringWorker()->dispatcher()) {
      io_uring_socket_->enable();
      io_uring_socket_->enableCloseEvent(events & Event::FileReadyType::Closed);
    } else {
      ENVOY_LOG(trace,
                "initialize file event on another thread, fd = {}, io_uring_socket_type = {}", fd_,
                ioUringSocketTypeStr());
      Thread::CondVar wait_cv;
      Thread::MutexBasicLockable mutex;
      Buffer::OwnedImpl buf;
      auto fd = io_uring_socket_->fd();

      mutex.lock();
      // Close the original socket at its running thread.
      io_uring_socket_->getIoUringWorker().dispatcher().post([&origin_socket = io_uring_socket_,
                                                              &wait_cv, &mutex, &buf]() {
        // Move the data of original socket's read buffer after the original socket read request
        // is done.
        origin_socket->close(true, [&origin_socket, &wait_cv, &mutex, &buf]() {
          mutex.lock();
          buf.move(dynamic_cast<Io::IoUringServerSocket*>(origin_socket.ptr())->getReadBuffer());
          wait_cv.notifyOne();
          mutex.unlock();
        });
      });

      wait_cv.wait(mutex);
      mutex.unlock();

      io_uring_socket_ = io_uring_factory_.getIoUringWorker()->addServerSocket(
          fd, buf, *this, events & Event::FileReadyType::Closed);
    }
    cb_ = std::move(cb);
    return;
  }

  switch (io_uring_socket_type_) {
  case IoUringSocketType::Server: {
    ENVOY_LOG(trace, "initialize file event for server socket, fd = {}", fd_);
    if (!enable_server_socket_) {
      ENVOY_LOG(trace, "fallback to IoSocketHandle for server socket");
      shadow_io_handle_->initializeFileEvent(dispatcher, std::move(cb), trigger, events);
      return;
    }

    io_uring_socket_ = io_uring_factory_.getIoUringWorker()->addServerSocket(
        fd_, *this, events & Event::FileReadyType::Closed);
    break;
  }
  case IoUringSocketType::Accept: {
    ENVOY_LOG(trace, "initialize file event for accept socket, fd = {}", fd_);
    if (!enable_accept_socket_) {
      ENVOY_LOG(trace, "fallback to IoSocketHandle for accept socket");
      shadow_io_handle_->initializeFileEvent(dispatcher, std::move(cb), trigger, events);
      return;
    } else {
      io_uring_socket_ = io_uring_factory_.getIoUringWorker()->addAcceptSocket(
          fd_, *this, events & Event::FileReadyType::Closed);
    }
    break;
  }
  case IoUringSocketType::Client:
  case IoUringSocketType::Unknown:
    ENVOY_LOG(trace, "initialize file event for client socket, fd = {}", fd_);
    io_uring_socket_type_ = IoUringSocketType::Client;
    if (!enable_client_socket_) {
      ENVOY_LOG(trace, "fallback to IoSocketHandle for client socket");
      shadow_io_handle_ = std::make_unique<IoSocketHandleImpl>(fd_, socket_v6only_, domain_);
      shadow_io_handle_->setBlocking(false);
      shadow_io_handle_->initializeFileEvent(dispatcher, std::move(cb), trigger, events);
      return;
    }
  }

  cb_ = std::move(cb);
}

void IoUringSocketHandleImpl::activateFileEvents(uint32_t events) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  ENVOY_LOG(trace, "activate file events {}, fd = {}, io_uring_socket_type = {}", events, fd_,
            ioUringSocketTypeStr());

  if ((io_uring_socket_type_ == IoUringSocketType::Client && !enable_client_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Server && !enable_server_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Accept && !enable_accept_socket_)) {
    shadow_io_handle_->activateFileEvents(events);
    return;
  }

  if (events & Event::FileReadyType::Read) {
    io_uring_socket_->injectCompletion(Io::RequestType::Read);
  }
  if (events & Event::FileReadyType::Write) {
    io_uring_socket_->injectCompletion(Io::RequestType::Write);
  }
}

void IoUringSocketHandleImpl::enableFileEvents(uint32_t events) {
  ENVOY_LOG(trace, "enable file events {}, fd = {}, io_uring_socket_type = {}", events, fd_,
            ioUringSocketTypeStr());
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);

  if ((io_uring_socket_type_ == IoUringSocketType::Client && !enable_client_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Server && !enable_server_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Accept && !enable_accept_socket_)) {
    shadow_io_handle_->enableFileEvents(events);
    return;
  }

  if (events & Event::FileReadyType::Read) {
    io_uring_socket_->enable();
  } else {
    io_uring_socket_->disable();
  }
  io_uring_socket_->enableCloseEvent(events & Event::FileReadyType::Closed);
}

void IoUringSocketHandleImpl::resetFileEvents() {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  ENVOY_LOG(trace, "reset file, fd = {}, io_uring_socket_type = {}", fd_, ioUringSocketTypeStr());

  if ((io_uring_socket_type_ == IoUringSocketType::Client && !enable_client_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Server && !enable_server_socket_) ||
      (io_uring_socket_type_ == IoUringSocketType::Accept && !enable_accept_socket_)) {
    shadow_io_handle_->resetFileEvents();
    return;
  }

  io_uring_socket_->disable();
  io_uring_socket_->enableCloseEvent(false);
}

IoHandlePtr IoUringSocketHandleImpl::duplicate() {
  ENVOY_LOG(trace, "duplicate, fd = {},io_uring_socket_type = {}", fd_, ioUringSocketTypeStr());
  auto result = Api::OsSysCallsSingleton::get().duplicate(fd_);
  RELEASE_ASSERT(result.return_value_ != -1,
                 fmt::format("duplicate failed for '{}': ({}) {}", fd_, result.errno_,
                             errorDetails(result.errno_)));
  return SocketInterfaceImpl::makePlatformSpecificSocket(result.return_value_, socket_v6only_,
                                                         domain_, &io_uring_factory_);
}

void IoUringSocketHandleImpl::onAcceptSocket(Io::AcceptedSocketParam& param) {
  accepted_socket_param_ = param;
  ENVOY_LOG(trace, "before on accept socket");
  cb_(Event::FileReadyType::Read);
  ENVOY_LOG(trace, "after on accept socket");

  // After accept the socket, the accepted_socket_param expected to be cleanup.
  ASSERT(accepted_socket_param_ == absl::nullopt);
}

void IoUringSocketHandleImpl::onRead(Io::ReadParam& param) {
  read_param_ = param;
  ENVOY_LOG(trace,
            "calling event callback since pending read buf has {} size data, data = {}, "
            "io_uring_socket_type = {}, fd = {}",
            read_param_->buf_.length(), read_param_->buf_.toString(), ioUringSocketTypeStr(), fd_);
  if (!SOCKET_VALID(fd_)) {
    ENVOY_LOG(trace, "The socket already closed, ignore this read event");
    return;
  }
  ASSERT(cb_ != nullptr);
  cb_(Event::FileReadyType::Read);
  read_param_ = absl::nullopt;
}

void IoUringSocketHandleImpl::onWrite(Io::WriteParam& param) {
  write_param_ = param;
  ENVOY_LOG(trace, "call event callback for write since result = {}", write_param_->result_);
  cb_(Event::FileReadyType::Write);
  write_param_ = absl::nullopt;
}

void IoUringSocketHandleImpl::onRemoteClose() {
  ASSERT(cb_ != nullptr);
  ENVOY_LOG(trace, "onRemoteClose fd = {}", fd_);
  cb_(Event::FileReadyType::Closed);
}

void IoUringSocketHandleImpl::onLocalClose() {
  ENVOY_LOG(trace, "onLocalClose fd = {}", fd_);
  io_uring_socket_.reset();
}

Api::IoCallUint64Result IoUringSocketHandleImpl::copyOut(uint64_t max_length,
                                                         Buffer::RawSlice* slices,
                                                         uint64_t num_slice) {
  if (read_param_ == absl::nullopt) {
    if (io_uring_socket_->getStatus() != Io::IoUringSocketStatus::RemoteClosed) {
      return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                                 IoSocketError::deleteIoError)};
    } else {
      ENVOY_LOG(trace, "readv remote close");
      return Api::ioCallUint64ResultNoError();
    }
  }

  ASSERT(io_uring_socket_.has_value());
  ENVOY_LOG(trace, "readv available, result = {}, fd = {}, type = {}", read_param_->result_, fd_,
            ioUringSocketTypeStr());

  if (read_param_->result_ == 0) {
    ENVOY_LOG(trace, "readv remote close");
    return Api::ioCallUint64ResultNoError();
  }

  if (read_param_->result_ < 0) {
    ASSERT(read_param_->buf_.length() == 0);
    if (read_param_->result_ == -EAGAIN) {
      return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                                 IoSocketError::deleteIoError)};
    }
    return {
        0, Api::IoErrorPtr(new IoSocketError(-read_param_->result_), IoSocketError::deleteIoError)};
  }

  // This mean the buffer ready read by previous call, return EAGAIN to tell the
  // caller waiting for next read event.
  if (read_param_->buf_.length() == 0) {
    return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                               IoSocketError::deleteIoError)};
  }

  const uint64_t max_read_length =
      std::min(max_length, static_cast<uint64_t>(read_param_->result_));
  uint64_t num_bytes_to_read =
      read_param_->buf_.copyOutToSlices(max_read_length, slices, num_slice);
  return {num_bytes_to_read, Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError)};
}

} // namespace Network
} // namespace Envoy
