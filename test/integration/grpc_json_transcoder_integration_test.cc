#include "common/grpc/codec.h"
#include "common/grpc/common.h"
#include "common/http/message_impl.h"

#include "test/integration/integration.h"
#include "test/mocks/http/mocks.h"
#include "test/proto/bookstore.pb.h"

#include "google/protobuf/stubs/status.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"

using google::protobuf::util::MessageDifferencer;
using google::protobuf::util::Status;
using google::protobuf::util::error::Code;
using google::protobuf::Empty;
using google::protobuf::Message;
using google::protobuf::TextFormat;

namespace Envoy {

class GrpcJsonTranscoderIntegrationTest
    : public BaseIntegrationTest,
      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  GrpcJsonTranscoderIntegrationTest() : BaseIntegrationTest(GetParam()) {}
  /**
   * Global initializer for all integration tests.
   */
  void SetUp() override {
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    createTestServer("test/config/integration/server_grpc_json_transcoder.json", {"http"});
  }

  /**
   * Global destructor for all integration tests.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

protected:
  template <class RequestType, class ResponseType>
  void testTranscoding(Http::HeaderMap&& request_headers, const std::string& request_body,
                       const std::vector<std::string>& grpc_request_messages,
                       const std::vector<std::string>& grpc_response_messages,
                       const Status& grpc_status, Http::HeaderMap&& response_headers,
                       const std::string& response_body) {
    IntegrationCodecClientPtr codec_client;
    FakeHttpConnectionPtr fake_upstream_connection;
    IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
    FakeStreamPtr request_stream;

    codec_client = makeHttpConnection(lookupPort("http"), Http::CodecClient::Type::HTTP1);

    if (!request_body.empty()) {
      Http::StreamEncoder& encoder = codec_client->startRequest(request_headers, *response);
      Buffer::OwnedImpl body(request_body);
      codec_client->sendData(encoder, body, true);
    } else {
      codec_client->makeHeaderOnlyRequest(request_headers, *response);
    }

    fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
    request_stream = fake_upstream_connection->waitForNewStream();
    if (!grpc_request_messages.empty()) {
      request_stream->waitForEndStream(*dispatcher_);

      Grpc::Decoder grpc_decoder;
      std::vector<Grpc::Frame> frames;
      EXPECT_TRUE(grpc_decoder.decode(request_stream->body(), frames));
      EXPECT_EQ(grpc_request_messages.size(), frames.size());

      for (size_t i = 0; i < grpc_request_messages.size(); ++i) {
        RequestType actual_message;
        if (frames[i].length_ > 0) {
          EXPECT_TRUE(actual_message.ParseFromArray(frames[i].data_->linearize(frames[i].length_),
                                                    frames[i].length_));
        }
        RequestType expected_message;
        EXPECT_TRUE(TextFormat::ParseFromString(grpc_request_messages[i], &expected_message));

        EXPECT_TRUE(MessageDifferencer::Equivalent(expected_message, actual_message));
      }

      Http::TestHeaderMapImpl response_headers;
      response_headers.insertStatus().value(200);
      response_headers.insertContentType().value(std::string("application/grpc"));
      if (grpc_response_messages.empty()) {
        response_headers.insertGrpcStatus().value(grpc_status.error_code());
        response_headers.insertGrpcMessage().value(grpc_status.error_message());
        request_stream->encodeHeaders(response_headers, true);
      } else {
        request_stream->encodeHeaders(response_headers, false);
        for (const auto& response_message_str : grpc_response_messages) {
          ResponseType response_message;
          EXPECT_TRUE(TextFormat::ParseFromString(response_message_str, &response_message));
          auto buffer = Grpc::Common::serializeBody(response_message);
          request_stream->encodeData(*buffer, false);
        }
        Http::TestHeaderMapImpl response_trailers;
        response_trailers.insertGrpcStatus().value(grpc_status.error_code());
        response_trailers.insertGrpcMessage().value(grpc_status.error_message());
        request_stream->encodeTrailers(response_trailers);
      }
      EXPECT_TRUE(request_stream->complete());
    } else {
      request_stream->waitForReset();
    }

    response->waitForEndStream();
    EXPECT_TRUE(response->complete());
    response_headers.iterate([](const Http::HeaderEntry& entry, void* context) -> void {
      IntegrationStreamDecoder* response = static_cast<IntegrationStreamDecoder*>(context);
      Http::LowerCaseString lower_key{entry.key().c_str()};
      EXPECT_STREQ(entry.value().c_str(), response->headers().get(lower_key)->value().c_str());
    }, response.get());
    if (!response_body.empty()) {
      EXPECT_EQ(response_body, response->body());
    }

    codec_client->close();
    fake_upstream_connection->close();
    fake_upstream_connection->waitForDisconnect();
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, GrpcJsonTranscoderIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryPost) {
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestHeaderMapImpl{{":method", "POST"},
                              {":path", "/shelf"},
                              {":authority", "host"},
                              {"content-type", "application/json"}},
      R"({"theme": "Children"})", {R"(shelf { theme: "Children" })"},
      {R"(id: 20 theme: "Children" )"}, Status::OK,
      Http::TestHeaderMapImpl{{":status", "200"},
                              {"content-type", "application/json"},
                              {"content-length", "30"},
                              {"grpc-status", "0"}},
      R"({"id":"20","theme":"Children"})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryGet) {
  testTranscoding<Empty, bookstore::ListShelvesResponse>(
      Http::TestHeaderMapImpl{{":method", "GET"}, {":path", "/shelves"}, {":authority", "host"}},
      "", {""}, {R"(shelves { id: 20 theme: "Children" }
          shelves { id: 1 theme: "Foo" } )"},
      Status::OK, Http::TestHeaderMapImpl{{":status", "200"},
                                          {"content-type", "application/json"},
                                          {"content-length", "69"},
                                          {"grpc-status", "0"}},
      R"({"shelves":[{"id":"20","theme":"Children"},{"id":"1","theme":"Foo"}]})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryGetError) {
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/100?"}, {":authority", "host"}},
      "", {"shelf: 100"}, {}, Status(Code::NOT_FOUND, "Shelf 100 Not Found"),
      Http::TestHeaderMapImpl{
          {":status", "200"}, {"grpc-status", "5"}, {"grpc-message", "Shelf 100 Not Found"}},
      "");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryDelete) {
  testTranscoding<bookstore::DeleteBookRequest, Empty>(
      Http::TestHeaderMapImpl{
          {":method", "DELETE"}, {":path", "/shelves/456/books/123"}, {":authority", "host"}},
      "", {"shelf: 456 book: 123"}, {""}, Status::OK,
      Http::TestHeaderMapImpl{{":status", "200"},
                              {"content-type", "application/json"},
                              {"content-length", "2"},
                              {"grpc-status", "0"}},
      "{}");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryPatch) {
  testTranscoding<bookstore::UpdateBookRequest, bookstore::Book>(
      Http::TestHeaderMapImpl{
          {":method", "PATCH"}, {":path", "/shelves/456/books/123"}, {":authority", "host"}},
      R"({"author" : "Leo Tolstoy", "title" : "War and Peace"})",
      {R"(shelf: 456 book { id: 123 author: "Leo Tolstoy" title: "War and Peace" })"},
      {R"(id: 123 author: "Leo Tolstoy" title: "War and Peace")"}, Status::OK,
      Http::TestHeaderMapImpl{{":status", "200"},
                              {"content-type", "application/json"},
                              {"content-length", "59"},
                              {"grpc-status", "0"}},
      R"({"id":"123","author":"Leo Tolstoy","title":"War and Peace"})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryCustom) {
  testTranscoding<bookstore::GetShelfRequest, Empty>(
      Http::TestHeaderMapImpl{
          {":method", "OPTIONS"}, {":path", "/shelves/456"}, {":authority", "host"}},
      "", {"shelf: 456"}, {""}, Status::OK,
      Http::TestHeaderMapImpl{{":status", "200"},
                              {"content-type", "application/json"},
                              {"content-length", "2"},
                              {"grpc-status", "0"}},
      "{}");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, BindingAndBody) {
  testTranscoding<bookstore::CreateBookRequest, bookstore::Book>(
      Http::TestHeaderMapImpl{
          {":method", "PUT"}, {":path", "/shelves/1/books"}, {":authority", "host"}},
      R"({"author" : "Leo Tolstoy", "title" : "War and Peace"})",
      {R"(shelf: 1 book { author: "Leo Tolstoy" title: "War and Peace" })"},
      {R"(id: 3 author: "Leo Tolstoy" title: "War and Peace")"}, Status::OK,
      Http::TestHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      R"({"id":"3","author":"Leo Tolstoy","title":"War and Peace"})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, ServerStreamingGet) {
  testTranscoding<bookstore::ListBooksRequest, bookstore::Book>(
      Http::TestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/1/books"}, {":authority", "host"}},
      "", {"shelf: 1"}, {R"(id: 1 author: "Neal Stephenson" title: "Readme")",
                         R"(id: 2 author: "George R.R. Martin" title: "A Game of Thrones")"},
      Status::OK, Http::TestHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      R"([{"id":"1","author":"Neal Stephenson","title":"Readme"})"
      R"(,{"id":"2","author":"George R.R. Martin","title":"A Game of Thrones"}])");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, StreamingPost) {
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestHeaderMapImpl{
          {":method", "POST"}, {":path", "/bulk/shelves"}, {":authority", "host"}},
      R"([
        { "theme" : "Classics" },
        { "theme" : "Satire" },
        { "theme" : "Russian" },
        { "theme" : "Children" },
        { "theme" : "Documentary" },
        { "theme" : "Mystery" },
      ])",
      {R"(shelf { theme: "Classics" })",
       R"(shelf { theme: "Satire" })",
       R"(shelf { theme: "Russian" })",
       R"(shelf { theme: "Children" })",
       R"(shelf { theme: "Documentary" })",
       R"(shelf { theme: "Mystery" })"},
      {R"(id: 3 theme: "Classics")",
       R"(id: 4 theme: "Satire")",
       R"(id: 5 theme: "Russian")",
       R"(id: 6 theme: "Children")",
       R"(id: 7 theme: "Documentary")",
       R"(id: 8 theme: "Mystery")"},
      Status::OK, Http::TestHeaderMapImpl{{":status", "200"},
                                          {"content-type", "application/json"},
                                          {"transfer-encoding", "chunked"}},
      R"([{"id":"3","theme":"Classics"})"
      R"(,{"id":"4","theme":"Satire"})"
      R"(,{"id":"5","theme":"Russian"})"
      R"(,{"id":"6","theme":"Children"})"
      R"(,{"id":"7","theme":"Documentary"})"
      R"(,{"id":"8","theme":"Mystery"}])");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, InvalidJson) {
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestHeaderMapImpl{{":method", "POST"}, {":path", "/shelf"}, {":authority", "host"}},
      R"(INVALID_JSON)", {}, {}, Status::OK,
      Http::TestHeaderMapImpl{{":status", "400"}, {"content-type", "text/plain"}},
      "Unexpected token.\n"
      "INVALID_JSON\n"
      "^");

  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestHeaderMapImpl{{":method", "POST"}, {":path", "/shelf"}, {":authority", "host"}},
      R"({ "theme" : "Children")", {}, {}, Status::OK,
      Http::TestHeaderMapImpl{{":status", "400"}, {"content-type", "text/plain"}},
      "Unexpected end of string. Expected , or } after key:value pair.\n"
      "\n"
      "^");

  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestHeaderMapImpl{{":method", "POST"}, {":path", "/shelf"}, {":authority", "host"}},
      R"({ "theme"  "Children" })", {}, {}, Status::OK,
      Http::TestHeaderMapImpl{{":status", "400"}, {"content-type", "text/plain"}},
      "Expected : between key:value pair.\n"
      "{ \"theme\"  \"Children\" }\n"
      "           ^");
}

} // namespace Envoy