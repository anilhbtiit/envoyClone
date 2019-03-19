#include "extensions/filters/network/kafka/request_codec.h"

#include "test/mocks/server/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

class RequestDecoderTest : public testing::Test {
protected:
  template <typename T> void putInBuffer(T arg);

  Buffer::OwnedImpl buffer_;
};

class CapturingRequestCallback : public RequestCallback {
public:
  virtual void onMessage(MessageSharedPtr request) override;

  const std::vector<MessageSharedPtr>& getCaptured() const;

private:
  std::vector<MessageSharedPtr> captured_;
};

typedef std::shared_ptr<CapturingRequestCallback> CapturingRequestCallbackSharedPtr;

void CapturingRequestCallback::onMessage(MessageSharedPtr message) { captured_.push_back(message); }

const std::vector<MessageSharedPtr>& CapturingRequestCallback::getCaptured() const {
  return captured_;
}

TEST_F(RequestDecoderTest, shouldProduceAbortedMessageOnUnknownData) {
  // given
  // api keys have values below 100, so the messages generated in this loop should not be recognized
  const int16_t base_api_key = 0;
  std::vector<RequestHeader> sent_headers;
  for (int16_t i = 0; i < 1000; ++i) {
    const int16_t api_key = static_cast<int16_t>(base_api_key + i);
    const RequestHeader header = {api_key, 0, 0, "client-id"};
    const std::vector<unsigned char> data = std::vector<unsigned char>(1024);
    putInBuffer(ConcreteRequest<std::vector<unsigned char>>{header, data});
    sent_headers.push_back(header);
  }

  const InitialParserFactory& initial_parser_factory = InitialParserFactory::getDefaultInstance();
  const RequestParserResolver& request_parser_resolver =
      RequestParserResolver::getDefaultInstance();
  const CapturingRequestCallbackSharedPtr request_callback =
      std::make_shared<CapturingRequestCallback>();

  RequestDecoder testee{initial_parser_factory, request_parser_resolver, {request_callback}};

  // when
  testee.onData(buffer_);

  // then
  const std::vector<MessageSharedPtr>& received = request_callback->getCaptured();
  ASSERT_EQ(received.size(), sent_headers.size());

  for (size_t i = 0; i < received.size(); ++i) {
    const std::shared_ptr<UnknownRequest> request =
        std::dynamic_pointer_cast<UnknownRequest>(received[i]);
    ASSERT_NE(request, nullptr);
    ASSERT_EQ(request->request_header_, sent_headers[i]);
  }
}

// misc utilities
template <typename T> void RequestDecoderTest::putInBuffer(T arg) {
  MessageEncoderImpl serializer{buffer_};
  serializer.encode(arg);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
