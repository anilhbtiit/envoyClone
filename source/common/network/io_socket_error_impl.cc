#include "common/network/io_socket_error_impl.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Network {

Api::IoError::IoErrorCode IoSocketError::getErrorCode() const {
  switch (errno_) {
  case EAGAIN:
    ASSERT(this == IoSocketError::getIoSocketEagainInstance(),
           "Didn't use getIoSocketEagainInstance() to generate `Again`.");
    return IoErrorCode::Again;
  case ENOTSUP:
    return IoErrorCode::NoSupport;
  case EAFNOSUPPORT:
    return IoErrorCode::AddressFamilyNoSupport;
  case EINPROGRESS:
    return IoErrorCode::InProgress;
  case EPERM:
    return IoErrorCode::Permission;
  case EMSGSIZE:
    return IoErrorCode::MessageSize;
  case EINTR:
    return IoErrorCode::Interrupt;
  case EINVAL:
    return IoErrorCode::InvalidValue;
  case EADDRNOTAVAIL:
    return IoErrorCode::AddressNotAvailable;
  default:
    ENVOY_LOG_MISC(error, "Unknown error code {} details {}", errno_, ::strerror(errno_));
    return IoErrorCode::UnknownError;
  }
}

std::string IoSocketError::getErrorDetails() const { return ::strerror(errno_); }

IoSocketError* IoSocketError::getIoSocketEagainInstance() {
  static auto* instance = new IoSocketError(EAGAIN);
  return instance;
}

void IoSocketError::deleteIoError(Api::IoError* err) {
  ASSERT(err != nullptr);
  if (err != getIoSocketEagainInstance()) {
    delete err;
  }
}

} // namespace Network
} // namespace Envoy
