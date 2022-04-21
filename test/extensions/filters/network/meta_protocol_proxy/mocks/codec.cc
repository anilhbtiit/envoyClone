#include "test/extensions/filters/network/meta_protocol_proxy/mocks/codec.h"

#include <memory>

using testing::_;
using testing::ByMove;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

MockCodecFactory::MockCodecFactory() {
  ON_CALL(*this, requestDecoder())
      .WillByDefault(Return(ByMove(std::make_unique<NiceMock<MockRequestDecoder>>())));
  ON_CALL(*this, responseDecoder())
      .WillByDefault(Return(ByMove(std::make_unique<NiceMock<MockResponseDecoder>>())));
  ON_CALL(*this, requestEncoder())
      .WillByDefault(Return(ByMove(std::make_unique<NiceMock<MockRequestEncoder>>())));
  ON_CALL(*this, responseEncoder())
      .WillByDefault(Return(ByMove(std::make_unique<NiceMock<MockResponseEncoder>>())));
  ON_CALL(*this, messageCreator())
      .WillByDefault(Return(ByMove(std::make_unique<NiceMock<MockMessageCreator>>())));
}

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
