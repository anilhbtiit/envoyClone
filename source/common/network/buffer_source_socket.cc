#include "common/network/buffer_source_socket.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/http/headers.h"
#include "include/envoy/network/_virtual_includes/transport_socket_interface/envoy/network/transport_socket.h"

namespace Envoy {
namespace Network {

void BufferSourceSocket::setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) {
  ASSERT(!callbacks_);
  callbacks_ = &callbacks;
}

IoResult BufferSourceSocket::doRead(Buffer::Instance& buffer) {
  if (read_source_buf_ == nullptr) {
    return {PostIoAction::Close, 0, true};
  }
  uint64_t bytes_read = 0;
  if (read_source_buf_->length() > 0) {
    bytes_read = read_source_buf_->length();
    buffer.move(*read_source_buf_);
  }
  return {PostIoAction::KeepOpen, bytes_read, false};
}

IoResult BufferSourceSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  ASSERT(!shutdown_ || buffer.length() == 0);
  if (write_dest_buf_ == nullptr) {
    return {PostIoAction::Close, 0, true};
  }
  uint64_t bytes_written = 0;
  if (buffer.length() > 0) {
    bytes_written = buffer.length();
    write_dest_buf_->move(buffer);
  }
  return {PostIoAction::KeepOpen, bytes_written, end_stream};
}

std::string BufferSourceSocket::protocol() const { return EMPTY_STRING; }
absl::string_view BufferSourceSocket::failureReason() const { return EMPTY_STRING; }

void BufferSourceSocket::onConnected() { callbacks_->raiseEvent(ConnectionEvent::Connected); }

TransportSocketPtr
BufferSourceSocketFactory::createTransportSocket(TransportSocketOptionsSharedPtr) const {
  return std::make_unique<BufferSourceSocket>();
}

bool BufferSourceSocketFactory::implementsSecureTransport() const { return false; }
} // namespace Network
} // namespace Envoy
