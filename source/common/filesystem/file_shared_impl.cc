#include "common/filesystem/file_shared_impl.h"

#include <cstring>

namespace Envoy {
namespace Filesystem {

Api::IoError::IoErrorCode IoFileError::errorCode() const {
  switch (errno_) {
  case EBADF:
    return IoErrorCode::BadHandle;
  default:
    return IoErrorCode::UnknownError;
  }
}

std::string IoFileError::errorDetails() const { return ::strerror(errno_); }

Api::IoCallBoolResult FileSharedImpl::open() {
  if (isOpen()) {
    return resultSuccess<bool>(true);
  }

  openFile();
  return fd_ != -1 ? resultSuccess<bool>(true) : resultFailure<bool>(false, errno);
}

Api::IoCallSizeResult FileSharedImpl::write(absl::string_view buffer) {
  const ssize_t rc = writeFile(buffer);
  return rc != -1 ? resultSuccess<ssize_t>(rc) : resultFailure<ssize_t>(rc, errno);
};

Api::IoCallBoolResult FileSharedImpl::close() {
  ASSERT(isOpen());

  bool success = closeFile();
  fd_ = -1;
  return success ? resultSuccess<bool>(true) : resultFailure<bool>(false, errno);
}

bool FileSharedImpl::isOpen() const { return fd_ != -1; };

std::string FileSharedImpl::path() const { return path_; };

} // namespace Filesystem
} // namespace Envoy