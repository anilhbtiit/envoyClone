#pragma once

#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Api {

/**
 * Base class for any I/O error.
 */
class IoError {
public:
  enum class IoErrorCode {
    // No data available right now, try again later.
    Again,
    // Not supported.
    NoSupport,
    // Address family not supported.
    AddressFamilyNoSupport,
    // During non-blocking connect, the connection cannot be completed immediately.
    InProgress,
    // Permission denied.
    Permission,
    // Other error codes cannot be mapped to any one above in getErrorCode().
    UnknownError
  };
  virtual ~IoError() {}

  virtual IoErrorCode getErrorCode() const PURE;
  virtual std::string getErrorDetails() const PURE;
};

using IoErrorDeleterType = void (*)(IoError*);
using IoErrorPtr = std::unique_ptr<IoError, IoErrorDeleterType>;

/**
 * Basic type for return result which has a return code and error code defined
 * according to different implementations.
 * If the call succeeds, |err_| is nullptr and |rc_| is valid. Otherwise |err_|
 * can be passed into IoError::getErrorCode() to extract the error. In this
 * case, |rc_| is invalid.
 */
template <typename T> struct IoCallResult {
  IoCallResult(T rc, IoErrorPtr err) : rc_(rc), err_(std::move(err)) {}

  IoCallResult(IoCallResult<T>&& result) : rc_(result.rc_), err_(std::move(result.err_)) {}

  virtual ~IoCallResult() {}

  IoCallResult& operator=(IoCallResult&& result) {
    rc_ = result.rc_;
    err_ = std::move(result.err_);
    return *this;
  }

  T rc_;
  IoErrorPtr err_;
};

using IoCallUintResult = IoCallResult<uint64_t>;

#define IO_CALL_RESULT_NO_ERROR                                                                    \
  Api::IoCallUintResult(0, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}))

} // namespace Api
} // namespace Envoy
