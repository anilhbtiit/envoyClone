#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/api/api.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/filter.h"
#include "envoy/server/configuration.h"
#include "envoy/server/listener_manager.h"

#include "common/buffer/buffer_impl.h"
#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/lock_guard.h"
#include "common/common/thread.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"
#include "common/network/filter_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/stats/stats_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

namespace Envoy {
class FakeHttpConnection;

/**
 * Provides a fake HTTP stream for integration testing.
 */
class FakeStream : public Http::StreamDecoder,
                   public Http::StreamCallbacks,
                   Logger::Loggable<Logger::Id::testing> {
public:
  FakeStream(FakeHttpConnection& parent, Http::StreamEncoder& encoder);

  uint64_t bodyLength() { return body_.length(); }
  Buffer::Instance& body() { return body_; }
  bool complete() { return end_stream_; }
  void encode100ContinueHeaders(const Http::HeaderMapImpl& headers);
  void encodeHeaders(const Http::HeaderMapImpl& headers, bool end_stream);
  void encodeData(uint64_t size, bool end_stream);
  void encodeData(Buffer::Instance& data, bool end_stream);
  void encodeTrailers(const Http::HeaderMapImpl& trailers);
  void encodeResetStream();
  const Http::HeaderMap& headers() { return *headers_; }
  void setAddServedByHeader(bool add_header) { add_served_by_header_ = add_header; }
  const Http::HeaderMapPtr& trailers() { return trailers_; }
  void waitForHeadersComplete();
  void waitForData(Event::Dispatcher& client_dispatcher, uint64_t body_length);
  void waitForEndStream(Event::Dispatcher& client_dispatcher);
  void waitForReset();

  // gRPC convenience methods.
  void startGrpcStream();
  void finishGrpcStream(Grpc::Status::GrpcStatus status);
  template <class T> void sendGrpcMessage(const T& message) {
    auto serialized_response = Grpc::Common::serializeBody(message);
    encodeData(*serialized_response, false);
    ENVOY_LOG(debug, "Sent gRPC message: {}", message.DebugString());
  }
  template <class T> void decodeGrpcFrame(T& message) {
    EXPECT_GE(decoded_grpc_frames_.size(), 1);
    if (decoded_grpc_frames_[0].length_ == 0) {
      decoded_grpc_frames_.erase(decoded_grpc_frames_.begin());
      return;
    }
    Buffer::ZeroCopyInputStreamImpl stream(std::move(decoded_grpc_frames_[0].data_));
    EXPECT_TRUE(decoded_grpc_frames_[0].flags_ == Grpc::GRPC_FH_DEFAULT);
    EXPECT_TRUE(message.ParseFromZeroCopyStream(&stream));
    ENVOY_LOG(debug, "Received gRPC message: {}", message.DebugString());
    decoded_grpc_frames_.erase(decoded_grpc_frames_.begin());
  }
  template <class T> void waitForGrpcMessage(Event::Dispatcher& client_dispatcher, T& message) {
    ENVOY_LOG(debug, "Waiting for gRPC message...");
    if (!decoded_grpc_frames_.empty()) {
      decodeGrpcFrame(message);
      return;
    }
    waitForData(client_dispatcher, 5);
    {
      Thread::LockGuard lock(lock_);
      EXPECT_TRUE(grpc_decoder_.decode(body(), decoded_grpc_frames_));
    }
    if (decoded_grpc_frames_.size() < 1) {
      waitForData(client_dispatcher, grpc_decoder_.length());
      {
        Thread::LockGuard lock(lock_);
        EXPECT_TRUE(grpc_decoder_.decode(body(), decoded_grpc_frames_));
      }
    }
    decodeGrpcFrame(message);
    ENVOY_LOG(debug, "Received gRPC message: {}", message.DebugString());
  }

  // Http::StreamDecoder
  void decode100ContinueHeaders(Http::HeaderMapPtr&&) override {}
  void decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeTrailers(Http::HeaderMapPtr&& trailers) override;

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  virtual void setEndStream(bool end) { end_stream_ = end; }

protected:
  Http::HeaderMapPtr headers_;

private:
  FakeHttpConnection& parent_;
  Http::StreamEncoder& encoder_;
  Thread::MutexBasicLockable lock_;
  Thread::CondVar decoder_event_;
  Http::HeaderMapPtr trailers_;
  bool end_stream_{};
  Buffer::OwnedImpl body_;
  bool saw_reset_{};
  Grpc::Decoder grpc_decoder_;
  std::vector<Grpc::Frame> decoded_grpc_frames_;
  bool add_served_by_header_{};
};

typedef std::unique_ptr<FakeStream> FakeStreamPtr;

// Encapsulates various state and functionality related to sharing a Connection object across
// threads. With FakeUpstream fabricated objects, we have a Connection that is associated with a
// dispatcher on a thread managed by FakeUpstream. We want to be able to safely invoke methods on
// this object from other threads (e.g. the main test thread) and be able to track connection state
// (e.g. are we disconnected and the Connection is now possibly deleted). We managed this via a
// SharedConnectionWrapper that lives from when the Connection is added to the accepted connection
// queue and then through the lifetime of the Fake{Raw,Http}Connection that manages the Connection
// through activeu use.
class SharedConnectionWrapper : public Network::ConnectionCallbacks {
public:
  using DisconnectCallback = std::function<void()>;

  SharedConnectionWrapper(Network::Connection& connection, bool allow_unexpected_disconnects)
      : connection_(connection), allow_unexpected_disconnects_(allow_unexpected_disconnects) {
    connection_.addConnectionCallbacks(*this);
  }

  void addDisconnectCallback(DisconnectCallback callback) {
    Thread::LockGuard lock(lock_);
    disconnect_callbacks_.emplace_back(callback);
  }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override {
    // Throughout this entire function, we know that the connection_ cannot disappear, since this
    // callback is invoked prior to connection_ deferred delete. We also know by locking below, that
    // elsewhere where we also hold lock_, that the connection cannot disappear inside the locked
    // scope.
    Thread::LockGuard lock(lock_);
    disconnected_ = true;
    if (event == Network::ConnectionEvent::RemoteClose ||
        event == Network::ConnectionEvent::LocalClose) {
      for (const auto& disconnect_callback : disconnect_callbacks_) {
        disconnect_callback();
      }
    }
  }

  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  bool connected() {
    Thread::LockGuard lock(lock_);
    return !disconnected_;
  }

  // This provides direct access to the underlying connection, but only to const methods.
  // Stateful connection related methods should happen on the connection's dispatcher via
  // executeOnDispatcher.
  // TODO(htuch): This seems a sketchy pattern; even if we're using const methods, there may be
  // thread safety violations when crossing between the test thread and FakeUpstream thread.
  Network::Connection& connection() const { return connection_; }

  // Execute some function on the connection's dispatcher. This involves a cross-thread post and
  // wait-for-completion. If the connection is disconnected, either prior to post or when the
  // dispatcher schedules the callback, we silently ignore, unless panic_on_unexpected_disconnect_
  // is set.
  void executeOnDispatcher(std::function<void(Network::Connection&)> f) {
    Thread::LockGuard lock(lock_);
    if (disconnected_) {
      return;
    }
    Thread::CondVar callback_ready_event;
    connection_.dispatcher().post([this, f, &callback_ready_event]() -> void {
      // The use of connected() here, vs. !disconnected_, is because we want to use the lock_
      // acquisition to briefly serialize. This avoids us entering this completion and issuing a
      // notifyOne() until the wait() is ready to receive it below.
      if (connected()) {
        f(connection_);
      } else {
        RELEASE_ASSERT(allow_unexpected_disconnects_);
      }
      callback_ready_event.notifyOne();
    });
    callback_ready_event.wait(lock_);
  }

private:
  Network::Connection& connection_;
  Thread::MutexBasicLockable lock_;
  std::vector<DisconnectCallback> disconnect_callbacks_ GUARDED_BY(lock_);
  bool disconnected_ GUARDED_BY(lock_){};
  const bool allow_unexpected_disconnects_;
};

class QueuedConnectionWrapper;
typedef std::unique_ptr<QueuedConnectionWrapper> QueuedConnectionWrapperPtr;

/**
 * Wraps a raw Network::Connection in a safe way, such that the connection can
 * be placed in a queue for an arbitrary amount of time. It handles disconnects
 * that take place in the queued state by failing the test. Once a
 * QueuedConnectionWrapper object is instantiated by FakeHttpConnection or
 * FakeRawConnection, it no longer plays a role.
 * TODO(htuch): We can simplify the storage lifetime by destructing if/when
 * removeConnectionCallbacks is added.
 */
class QueuedConnectionWrapper {
public:
  QueuedConnectionWrapper(Network::Connection& connection, bool allow_unexpected_disconnects)
      : shared_connection_(connection, allow_unexpected_disconnects), parented_(false),
        allow_unexpected_disconnects_(allow_unexpected_disconnects) {
    shared_connection_.addDisconnectCallback([this] {
      Thread::LockGuard lock(lock_);
      RELEASE_ASSERT(parented_ || allow_unexpected_disconnects_);
    });
  }

  void set_parented() {
    Thread::LockGuard lock(lock_);
    parented_ = true;
  }

  SharedConnectionWrapper& shared_connection() { return shared_connection_; }

private:
  SharedConnectionWrapper shared_connection_;
  Thread::MutexBasicLockable lock_;
  bool parented_ GUARDED_BY(lock_);
  const bool allow_unexpected_disconnects_;
};

/**
 * Base class for both fake raw connections and fake HTTP connections.
 */
class FakeConnectionBase {
public:
  virtual ~FakeConnectionBase() { ASSERT(initialized_); }
  void close();
  void readDisable(bool disable);
  // By default waitForDisconnect and waitForHalfClose assume the next event is a disconnect and
  // fails an assert if an unexpected event occurs. If a caller truly wishes to wait until
  // disconnect, set ignore_spurious_events = true.
  void waitForDisconnect(bool ignore_spurious_events = false);
  void waitForHalfClose(bool ignore_spurious_events = false);

  virtual void initialize() {
    initialized_ = true;
    connection_wrapper_->set_parented();
    shared_connection_.addDisconnectCallback([this] { connection_event_.notifyOne(); });
  }
  void enableHalfClose(bool enabled);
  SharedConnectionWrapper& shared_connection() { return shared_connection_; }
  // The same caveats apply here as in SharedConnectionWrapper::connection().
  Network::Connection& connection() const { return shared_connection_.connection(); }
  bool connected() const { return shared_connection_.connected(); }

protected:
  FakeConnectionBase(QueuedConnectionWrapperPtr connection_wrapper)
      : shared_connection_(connection_wrapper->shared_connection()),
        connection_wrapper_(std::move(connection_wrapper)) {}

  SharedConnectionWrapper& shared_connection_;
  bool initialized_{};
  Thread::CondVar connection_event_;
  Thread::MutexBasicLockable lock_;
  bool half_closed_ GUARDED_BY(lock_){};

private:
  // We hold on to this as connection callbacks live for the entire life of the
  // connection.
  QueuedConnectionWrapperPtr connection_wrapper_;
};

/**
 * Provides a fake HTTP connection for integration testing.
 */
class FakeHttpConnection : public Http::ServerConnectionCallbacks, public FakeConnectionBase {
public:
  enum class Type { HTTP1, HTTP2 };

  FakeHttpConnection(QueuedConnectionWrapperPtr connection_wrapper, Stats::Store& store, Type type);
  // By default waitForNewStream assumes the next event is a new stream and
  // fails an assert if an unexpected event occurs. If a caller truly wishes to
  // wait for a new stream, set ignore_spurious_events = true.
  FakeStreamPtr waitForNewStream(Event::Dispatcher& client_dispatcher,
                                 bool ignore_spurious_events = false);

  // Http::ServerConnectionCallbacks
  Http::StreamDecoder& newStream(Http::StreamEncoder& response_encoder) override;
  void onGoAway() override { NOT_IMPLEMENTED; }

private:
  struct ReadFilter : public Network::ReadFilterBaseImpl {
    ReadFilter(FakeHttpConnection& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override {
      parent_.codec_->dispatch(data);
      return Network::FilterStatus::StopIteration;
    }

    FakeHttpConnection& parent_;
  };

  Http::ServerConnectionPtr codec_;
  std::list<FakeStreamPtr> new_streams_;
};

typedef std::unique_ptr<FakeHttpConnection> FakeHttpConnectionPtr;

/**
 * Fake raw connection for integration testing.
 */
class FakeRawConnection : Logger::Loggable<Logger::Id::testing>, public FakeConnectionBase {
public:
  FakeRawConnection(QueuedConnectionWrapperPtr connection_wrapper)
      : FakeConnectionBase(std::move(connection_wrapper)) {}

  std::string waitForData(uint64_t num_bytes);
  void write(const std::string& data, bool end_stream = false);

  void initialize() override {
    shared_connection_.executeOnDispatcher([this](Network::Connection& connection) {
      connection.addReadFilter(Network::ReadFilterSharedPtr{new ReadFilter(*this)});
    });
    FakeConnectionBase::initialize();
  }

private:
  struct ReadFilter : public Network::ReadFilterBaseImpl {
    ReadFilter(FakeRawConnection& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override;

    FakeRawConnection& parent_;
  };

  std::string data_;
};

typedef std::unique_ptr<FakeRawConnection> FakeRawConnectionPtr;

/**
 * Provides a fake upstream server for integration testing.
 */
class FakeUpstream : Logger::Loggable<Logger::Id::testing>,
                     public Network::FilterChainManager,
                     public Network::FilterChainFactory {
public:
  FakeUpstream(const std::string& uds_path, FakeHttpConnection::Type type);
  FakeUpstream(uint32_t port, FakeHttpConnection::Type type, Network::Address::IpVersion version,
               bool enable_half_close = false);
  FakeUpstream(Network::TransportSocketFactoryPtr&& transport_socket_factory, uint32_t port,
               FakeHttpConnection::Type type, Network::Address::IpVersion version);
  ~FakeUpstream();

  FakeHttpConnection::Type httpType() { return http_type_; }
  FakeHttpConnectionPtr waitForHttpConnection(Event::Dispatcher& client_dispatcher);
  FakeRawConnectionPtr
  waitForRawConnection(std::chrono::milliseconds wait_for_ms = std::chrono::milliseconds{10000});
  Network::Address::InstanceConstSharedPtr localAddress() const { return socket_->localAddress(); }

  // Wait for one of the upstreams to receive a connection
  static FakeHttpConnectionPtr
  waitForHttpConnection(Event::Dispatcher& client_dispatcher,
                        std::vector<std::unique_ptr<FakeUpstream>>& upstreams);

  // Network::FilterChainManager
  const Network::FilterChain* findFilterChain(const Network::ConnectionSocket&) const override {
    return filter_chain_.get();
  }

  // Network::FilterChainFactory
  bool
  createNetworkFilterChain(Network::Connection& connection,
                           const std::vector<Network::FilterFactoryCb>& filter_factories) override;
  bool createListenerFilterChain(Network::ListenerFilterManager& listener) override;
  void set_allow_unexpected_disconnects(bool value) { allow_unexpected_disconnects_ = value; }

protected:
  Stats::IsolatedStoreImpl stats_store_;
  const FakeHttpConnection::Type http_type_;
  void cleanUp();

private:
  FakeUpstream(Network::TransportSocketFactoryPtr&& transport_socket_factory,
               Network::SocketPtr&& connection, FakeHttpConnection::Type type,
               bool enable_half_close);

  class FakeListener : public Network::ListenerConfig {
  public:
    FakeListener(FakeUpstream& parent) : parent_(parent), name_("fake_upstream") {}

  private:
    // Network::ListenerConfig
    Network::FilterChainManager& filterChainManager() override { return parent_; }
    Network::FilterChainFactory& filterChainFactory() override { return parent_; }
    Network::Socket& socket() override { return *parent_.socket_; }
    bool bindToPort() override { return true; }
    bool handOffRestoredDestinationConnections() const override { return false; }
    uint32_t perConnectionBufferLimitBytes() override { return 0; }
    Stats::Scope& listenerScope() override { return parent_.stats_store_; }
    uint64_t listenerTag() const override { return 0; }
    const std::string& name() const override { return name_; }

    FakeUpstream& parent_;
    std::string name_;
  };

  void threadRoutine();

  Network::SocketPtr socket_;
  ConditionalInitializer server_initialized_;
  // Guards any objects which can be altered both in the upstream thread and the
  // main test thread.
  Thread::MutexBasicLockable lock_;
  Thread::ThreadPtr thread_;
  Thread::CondVar new_connection_event_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Network::ConnectionHandlerPtr handler_;
  std::list<QueuedConnectionWrapperPtr> new_connections_; // Guarded by lock_
  bool allow_unexpected_disconnects_;
  const bool enable_half_close_;
  FakeListener listener_;
  const Network::FilterChainSharedPtr filter_chain_;
};
} // namespace Envoy
