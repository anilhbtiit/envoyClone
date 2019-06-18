#include "mocks.h"

#include <cstdint>

#include "common/common/assert.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

void PrintTo(const RespValue& value, std::ostream* os) { *os << value.toString(); }

void PrintTo(const RespValuePtr& value, std::ostream* os) { *os << value->toString(); }

MockEncoder::MockEncoder() {
  ON_CALL(*this, encode(_, _))
      .WillByDefault(
          Invoke([this](const Common::Redis::RespValue& value, Buffer::Instance& out) -> void {
            real_encoder_.encode(value, out);
          }));
}

MockEncoder::~MockEncoder() {}

MockDecoder::MockDecoder() {}
MockDecoder::~MockDecoder() {}

namespace Client {

MockClient::MockClient() {
  ON_CALL(*this, addConnectionCallbacks(_))
      .WillByDefault(Invoke([this](Network::ConnectionCallbacks& callbacks) -> void {
        callbacks_.push_back(&callbacks);
      }));
  ON_CALL(*this, close()).WillByDefault(Invoke([this]() -> void {
    raiseEvent(Network::ConnectionEvent::LocalClose);
  }));
}

MockClient::~MockClient() {}

MockPoolRequest::MockPoolRequest() {}
MockPoolRequest::~MockPoolRequest() {}

MockPoolCallbacks::MockPoolCallbacks() {}
MockPoolCallbacks::~MockPoolCallbacks() {}

} // namespace Client

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
