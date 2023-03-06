#include "envoy/common/io/io_uring.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Io {

class MockIoUring : public IoUring {
public:
  MOCK_METHOD(os_fd_t, registerEventfd, ());
  MOCK_METHOD(void, unregisterEventfd, ());
  MOCK_METHOD(bool, isEventfdRegistered, (), (const));
  MOCK_METHOD(void, forEveryCompletion, (const CompletionCb& completion_cb));
  MOCK_METHOD(IoUringResult, prepareAccept,
              (os_fd_t fd, struct sockaddr* remote_addr, socklen_t* remote_addr_len,
               void* user_data));
  MOCK_METHOD(IoUringResult, prepareConnect,
              (os_fd_t fd, const Network::Address::InstanceConstSharedPtr& address,
               void* user_data));
  MOCK_METHOD(IoUringResult, prepareReadv,
              (os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs, off_t offset,
               void* user_data));
  MOCK_METHOD(IoUringResult, prepareWritev,
              (os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs, off_t offset,
               void* user_data));
  MOCK_METHOD(IoUringResult, prepareClose, (os_fd_t fd, void* user_data));
  MOCK_METHOD(IoUringResult, prepareCancel, (void* cancelling_user_data, void* user_data));
  MOCK_METHOD(IoUringResult, submit, ());
  MOCK_METHOD(void, injectCompletion, (os_fd_t fd, void* user_data, int32_t result));
  MOCK_METHOD(void, removeInjectedCompletion, (os_fd_t fd));
};

class MockIoUringFactory : public IoUringFactory {
public:
  MOCK_METHOD(OptRef<IoUringWorker>, getIoUringWorker, ());
  MOCK_METHOD(void, onServerInitialized, ());
  MOCK_METHOD(bool, currentThreadRegistered, ());
};

class MockIoUringSocket : public IoUringSocket {
  MOCK_METHOD(os_fd_t, fd, (), (const));
  MOCK_METHOD(void, close, ());
  MOCK_METHOD(void, enable, ());
  MOCK_METHOD(void, disable, ());
  MOCK_METHOD(void, connect, (const Network::Address::InstanceConstSharedPtr& address));
  MOCK_METHOD(uint64_t, write, (Buffer::Instance & data));
  MOCK_METHOD(uint64_t, writev, (const Buffer::RawSlice* slices, uint64_t num_slice));
  MOCK_METHOD(void, onAccept, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onConnect, (int32_t result, bool injected));
  MOCK_METHOD(void, onRead, (int32_t result, bool injected));
  MOCK_METHOD(void, onWrite, (int32_t result, bool injected));
  MOCK_METHOD(void, onClose, (int32_t result, bool injected));
  MOCK_METHOD(void, onCancel, (int32_t result, bool injected));
  MOCK_METHOD(void, injectCompletion, (uint32_t type));
  MOCK_METHOD(IoUringSocketStatus, getStatus, (), (const));
};

class MockIoUringWorker : public IoUringWorker {
public:
  MOCK_METHOD(IoUringSocket&, addAcceptSocket, (os_fd_t fd, IoUringHandler& handler));
  MOCK_METHOD(IoUringSocket&, addServerSocket,
              (os_fd_t fd, IoUringHandler& handler, uint32_t read_buffer_size));
  MOCK_METHOD(IoUringSocket&, addClientSocket,
              (os_fd_t fd, IoUringHandler& handler, uint32_t read_buffer_size));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(Request*, submitAcceptRequest, (IoUringSocket & socket));
  MOCK_METHOD(Request*, submitConnectRequest,
              (IoUringSocket & socket, const Network::Address::InstanceConstSharedPtr& address));
  MOCK_METHOD(Request*, submitReadRequest, (IoUringSocket & socket, struct iovec* iov));
  MOCK_METHOD(Request*, submitWritevRequest,
              (IoUringSocket & socket, struct iovec* iovecs, uint64_t num_vecs));
  MOCK_METHOD(Request*, submitCloseRequest, (IoUringSocket & socket));
  MOCK_METHOD(Request*, submitCancelRequest, (IoUringSocket & socket, Request* request_to_cancel));
};

} // namespace Io
} // namespace Envoy
