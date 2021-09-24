#include "source/common/network/listener_filter_buffer_impl.h"

#include <string>

namespace Envoy {
namespace Network {

const Buffer::ConstRawSlice ListenerFilterBufferImpl::rawSlice() const {
  Buffer::ConstRawSlice slice;
  slice.mem_ = base_;
  slice.len_ = data_size_;
  return slice;
}

bool ListenerFilterBufferImpl::drain(uint64_t length) {
  if (length == 0) {
    return true;
  }
  // Since we want to drain the data from the socket, so a
  // temporary buffer need here.
  std::unique_ptr<uint8_t[]> buf(new uint8_t[length]);
  uint64_t read_size = 0;
  while (1) {
    auto result = io_handle_.recv(buf.get(), length - read_size, 0);
    ENVOY_LOG(trace, "recv returned: {}", result.return_value_);

    if (!result.ok()) {
      if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
        continue;
      }
      ENVOY_LOG(debug, "recv failed: {}: {}", result.err_->getErrorCode(),
                result.err_->getErrorDetails());
      return false;
    }
    // Remote closed
    if (result.return_value_ == 0) {
      ENVOY_LOG(debug, "recv failed: remote closed");
      return false;
    }
    read_size += result.return_value_;
    if (read_size < length) {
      continue;
    }
    base_ += length;
    data_size_ -= length;
    buffer_size_ -= length;
    break;
  }
  return true;
}

PeekState ListenerFilterBufferImpl::peekFromSocket() {
  const auto result = io_handle_.recv(base_, buffer_size_, MSG_PEEK);
  ENVOY_LOG(trace, "recv returned: {}", result.return_value_);

  if (!result.ok()) {
    if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
      return PeekState::Again;
    }
    ENVOY_LOG(debug, "recv failed: {}: {}", result.err_->getErrorCode(),
              result.err_->getErrorDetails());
    return PeekState::Error;
  }
  // Remote closed
  if (result.return_value_ == 0) {
    ENVOY_LOG(debug, "recv failed: remote closed");
    return PeekState::Error;
  }
  data_size_ = result.return_value_;
  return PeekState::Done;
}

void ListenerFilterBufferImpl::onFileEvent(uint32_t events) {
  if (events & Event::FileReadyType::Closed) {
    on_close_cb_();
    return;
  }

  auto state = peekFromSocket();
  if (state == PeekState::Done) {
    on_data_cb_();
  } else if (state == PeekState::Error) {
    on_close_cb_();
  }
  // Did nothing for `Api::IoError::IoErrorCode::Again`
}

} // namespace Network
} // namespace Envoy
