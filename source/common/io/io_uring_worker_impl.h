#pragma once

#include "envoy/common/io/io_uring.h"
#include "envoy/event/file_event.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/io/io_uring_impl.h"

namespace Envoy {
namespace Io {

class IoUringWorkerImpl;

class IoUringSocketEntry : public IoUringSocket,
                           public LinkedObject<IoUringSocketEntry>,
                           public Event::DeferredDeletable,
                           protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringSocketEntry(os_fd_t fd, IoUringWorkerImpl& parent, IoUringHandler& io_uring_handler);

  // IoUringSocket
  os_fd_t fd() const override { return fd_; }
  void close() override { status_ = CLOSING; }
  void enable() override { status_ = ENABLED; }
  void disable() override { status_ = DISABLED; }
  void connect(const Network::Address::InstanceConstSharedPtr&) override {}
  uint64_t write(Buffer::Instance&) override { PANIC("not implement"); }
  uint64_t writev(const Buffer::RawSlice*, uint64_t) override { PANIC("not implement"); }
  // This will cleanup all the injected completions for this socket and
  // unlink itself from the worker.
  void cleanup();
  void onAccept(Request*, int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Accept)) {
      injected_completions_ &= ~RequestType::Accept;
    }
  }
  void onConnect(int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Connect)) {
      injected_completions_ &= ~RequestType::Connect;
    }
  }
  void onRead(int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Read)) {
      injected_completions_ &= ~RequestType::Read;
    }
  }
  void onWrite(int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Write)) {
      injected_completions_ &= ~RequestType::Write;
    }
  }
  void onClose(int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Close)) {
      injected_completions_ &= ~RequestType::Close;
    }
  }
  void onCancel(int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Cancel)) {
      injected_completions_ &= ~RequestType::Cancel;
    }
  }
  void injectCompletion(uint32_t type) override;
  IoUringSocketStatus getStatus() const override { return status_; }

protected:
  os_fd_t fd_;
  IoUringWorkerImpl& parent_;
  IoUringHandler& io_uring_handler_;
  uint32_t injected_completions_{0};
  IoUringSocketStatus status_{INITIALIZED};
};

using IoUringSocketEntryPtr = std::unique_ptr<IoUringSocketEntry>;

class IoUringWorkerImpl : public IoUringWorker, private Logger::Loggable<Logger::Id::io> {
public:
  IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                    Event::Dispatcher& dispatcher);
  IoUringWorkerImpl(IoUringPtr io_uring_instance, Event::Dispatcher& dispatcher);
  ~IoUringWorkerImpl() override;

  // IoUringWorker
  IoUringSocket& addAcceptSocket(os_fd_t fd, IoUringHandler& handler) override;
  IoUringSocket& addServerSocket(os_fd_t fd, IoUringHandler& handler,
                                 uint32_t read_buffer_size) override;
  IoUringSocket& addClientSocket(os_fd_t fd, IoUringHandler& handler,
                                 uint32_t read_buffer_size) override;

  Event::Dispatcher& dispatcher() override;

  Request* submitAcceptRequest(IoUringSocket& socket) override;
  Request* submitConnectRequest(IoUringSocket& socket,
                                const Network::Address::InstanceConstSharedPtr& address) override;
  Request* submitReadRequest(IoUringSocket& socket, struct iovec* iov) override;
  Request* submitWritevRequest(IoUringSocket& socket, struct iovec* iovecs,
                               uint64_t num_vecs) override;
  Request* submitCloseRequest(IoUringSocket& socket) override;
  Request* submitCancelRequest(IoUringSocket& socket, Request* request_to_cancel) override;

  // From socket from the worker.
  IoUringSocketEntryPtr removeSocket(IoUringSocketEntry& socket);
  // Inject a request completion into the io_uring instance.
  void injectCompletion(IoUringSocket& socket, uint32_t type, int32_t result);
  // Remove all the injected completion for the specific socket.
  void removeInjectedCompletion(IoUringSocket& socket);

protected:
  void onFileEvent();
  void submit();

  // The io_uring instance.
  IoUringPtr io_uring_instance_;
  // The file event of io_uring's eventfd.
  Event::FileEventPtr file_event_{nullptr};
  Event::Dispatcher& dispatcher_;
  // All the sockets in this worker.
  std::list<IoUringSocketEntryPtr> sockets_;
  // This is used to mark whether delay submit is enabled.
  // The IoUriingWorks delay the submit the requests which are submitted in request completion
  // callback.
  bool delay_submit_{false};
};

struct AcceptRequest : public Request {
  sockaddr_storage remote_addr_{};
  socklen_t remote_addr_len_{sizeof(remote_addr_)};
};

class IoUringAcceptSocket : public IoUringSocketEntry {
public:
  IoUringAcceptSocket(os_fd_t fd, IoUringWorkerImpl& parent, IoUringHandler& io_uring_handler,
                      int max_requests = 5);

  void close() override;
  void disable() override;
  void enable() override;
  void onClose(int32_t result, bool injected) override;
  void onAccept(Request* req, int32_t result, bool injected) override;
  void submitRequests();

private:
  // TODO(soulxu): making this configurable.
  int max_requests_{0};
  // This is used to track the current submitted accept requests.
  absl::flat_hash_set<Request*> requests_;
};

} // namespace Io
} // namespace Envoy
