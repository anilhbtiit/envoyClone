#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Tracing {

MockDecorator::MockDecorator() {}
MockDecorator::~MockDecorator() {}

MockSpan::MockSpan() {}
MockSpan::~MockSpan() {}

MockFinalizer::MockFinalizer() {}
MockFinalizer::~MockFinalizer() {}

MockConfig::MockConfig() {
  ON_CALL(*this, operationName()).WillByDefault(Return(operation_name_));
  ON_CALL(*this, requestHeadersForTags()).WillByDefault(ReturnRef(headers_));
}
MockConfig::~MockConfig() {}

MockHttpTracer::MockHttpTracer() {}
MockHttpTracer::~MockHttpTracer() {}

MockDriver::MockDriver() {}
MockDriver::~MockDriver() {}

} // namespace Tracing
} // namespace Envoy
