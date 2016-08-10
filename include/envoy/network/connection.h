#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/network/filter.h"
#include "envoy/ssl/connection.h"

namespace Event {
class Dispatcher;
}

namespace Network {

/**
 * Events that occur on a connection. Maybe be combined.
 */
class ConnectionEvent {
public:
  static const uint32_t RemoteClose = 0x1;
  static const uint32_t LocalClose = 0x2;
  static const uint32_t Connected = 0x4;
};

/**
 * Connections have both a read and write buffer.
 */
enum class ConnectionBufferType { Read, Write };

/**
 * Network level callbacks that happen on a connection.
 */
class ConnectionCallbacks {
public:
  virtual ~ConnectionCallbacks() {}

  /**
   * Callback for connection buffer changes.
   * @param type supplies which buffer has changed.
   * @param old_size supplies the original size of the buffer.
   * @param delta supplies how much data was added or removed from the buffer.
   */
  virtual void onBufferChange(ConnectionBufferType type, uint64_t old_size, int64_t delta) PURE;

  /**
   * Callback for connection events.
   * @param events supplies the ConnectionEvent events that occurred as a bitmask.
   */
  virtual void onEvent(uint32_t events) PURE;
};

/**
 * Type of connection close to perform.
 */
enum class ConnectionCloseType {
  FlushWrite, // Flush pending write data before raising ConnectionEvent::LocalClose
  NoFlush     // Do not flush any pending data and immediately raise ConnectionEvent::LocalClose
};

/**
 * An abstract raw connection. Free the connection or call close() to disconnect.
 */
class Connection : public Event::DeferredDeletable {
public:
  enum class State { Open, Closing, Closed };

  virtual ~Connection() {}

  /**
   * Register callbacks that fire when connection events occur.
   */
  virtual void addConnectionCallbacks(ConnectionCallbacks& cb) PURE;

  /**
   * Add a write filter to the connection. Filters are invoked in LIFO order (the last added
   * filter is called first).
   */
  virtual void addWriteFilter(WriteFilterPtr filter) PURE;

  /**
   * Add a combination filter to the connection. Equivalent to calling both addWriteFilter()
   * and addReadFilter() with the same filter instance.
   */
  virtual void addFilter(FilterPtr filter) PURE;

  /**
   * Add a read filter to the connection. Filters are invoked in FIFO order (the filter added
   * first is called first).
   */
  virtual void addReadFilter(ReadFilterPtr filter) PURE;

  /**
   * Close the connection.
   */
  virtual void close(ConnectionCloseType type) PURE;

  /**
   * @return Event::Dispatcher& the dispatcher backing this connection.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * @return uint64_t the unique local ID of this connection.
   */
  virtual uint64_t id() PURE;

  /**
   * @return std::string the next protocol to use as selected by network level negotiation. (E.g.,
   *         ALPN). If network level negotation is not supported by the connection or no protocol
   *         has been negotiated the empty string is returned.
   */
  virtual std::string nextProtocol() PURE;

  /**
   * Enable/Disable TCP NO_DELAY on the connection.
   */
  virtual void noDelay(bool enable) PURE;

  /**
   * Disable socket reads on the connection, applying external back pressure. When reads are
   * enabled again if there is data still in the input buffer it will be redispatched through
   * the filter chain.
   * @param disable supplies TRUE is reads should be disabled, FALSE if they should be enabled.
   */
  virtual void readDisable(bool disable) PURE;

  /**
   * @return bool whether reading is enabled on the connection.
   */
  virtual bool readEnabled() PURE;

  /**
   * @return The address of the remote client
   */
  virtual const std::string& remoteAddress() PURE;

  /**
   * @return the SSL connection data if this is an SSL connection, or nullptr if it is not.
   */
  virtual Ssl::Connection* ssl() PURE;

  /**
   * @return State the current state of the connection.
   */
  virtual State state() PURE;

  /**
   * Write data to the connection. Will iterate through downstream filters with the buffer if any
   * are installed.
   */
  virtual void write(Buffer::Instance& data) PURE;
};

typedef std::unique_ptr<Connection> ConnectionPtr;

/**
 * Connections capable of outbound connects.
 */
class ClientConnection : public virtual Connection {
public:
  /**
   * Connect to a remote host. Errors or connection events are reported via the event callback
   * registered via setConnectionEventCb().
   */
  virtual void connect() PURE;
};

typedef std::unique_ptr<ClientConnection> ClientConnectionPtr;

} // Network
