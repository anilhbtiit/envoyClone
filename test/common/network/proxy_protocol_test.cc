#include "common/buffer/buffer_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/listener_impl.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;

namespace Network {

class ProxyProtocolTest : public testing::Test {
public:
  ProxyProtocolTest()
      : socket_(1234), listener_(dispatcher_, socket_, callbacks_, stats_store_, true) {
    conn_ = dispatcher_.createClientConnection("tcp://127.0.0.1:1234");
    conn_->addConnectionCallbacks(connection_callbacks_);
    conn_->connect();
  }

  void write(const std::string& s) {
    Buffer::OwnedImpl buf(s);
    conn_->write(buf);
  }

  Event::DispatcherImpl dispatcher_;
  TcpListenSocket socket_;
  Stats::IsolatedStoreImpl stats_store_;
  MockListenerCallbacks callbacks_;
  ListenerImpl listener_;
  ClientConnectionPtr conn_;
  NiceMock<MockConnectionCallbacks> connection_callbacks_;
  std::shared_ptr<MockReadFilter> read_filter_;
};

TEST_F(ProxyProtocolTest, Basic) {

  write("PROXY TCP4 1.2.3.4 255.255.255.255 66776 1234\r\nmore data");

  ConnectionPtr accepted_connection;

  EXPECT_CALL(callbacks_, onNewConnection_(_))
      .WillOnce(Invoke([&](ConnectionPtr& conn) -> void {
        ASSERT_EQ("1.2.3.4", conn->remoteAddress());
        conn->addReadFilter(read_filter_);
        accepted_connection = std::move(conn);
      }));

  read_filter_.reset(new MockReadFilter());
  EXPECT_CALL(*read_filter_.get(), onData(BufferStringEqual("more data")));

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  accepted_connection->close(ConnectionCloseType::NoFlush);
  conn_->close(ConnectionCloseType::NoFlush);
}

TEST_F(ProxyProtocolTest, Fragmented) {

  write("PROXY TCP4");
  write(" 255.255.2");
  write("55.255 1.2");
  write(".3.4 66776");
  write(" 1234\r\n");

  EXPECT_CALL(callbacks_, onNewConnection_(_))
      .WillOnce(Invoke([&](ConnectionPtr& conn) -> void {
        ASSERT_EQ("255.255.255.255", conn->remoteAddress());
        read_filter_.reset(new MockReadFilter());
        conn->addReadFilter(read_filter_);
        conn->close(ConnectionCloseType::NoFlush);
      }));

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(ProxyProtocolTest, PartialRead) {

  write("PROXY TCP4");
  write(" 255.255.2");

  EXPECT_CALL(callbacks_, onNewConnection_(_))
      .WillOnce(Invoke([&](ConnectionPtr& conn) -> void {
        ASSERT_EQ("255.255.255.255", conn->remoteAddress());
        read_filter_.reset(new MockReadFilter());
        conn->addReadFilter(read_filter_);
        conn->close(ConnectionCloseType::NoFlush);
      }));

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);

  write("55.255 1.2");
  write(".3.4 66776");
  write(" 1234\r\n");

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(ProxyProtocolTest, MalformedProxyLine) {
  write("BOGUS\r\n");
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::Connected));
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(ProxyProtocolTest, ProxyLineTooLarge) {
  write("012345678901234567890123456789012345678901234567890123456789\r\n");
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::Connected));
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

} // Network
