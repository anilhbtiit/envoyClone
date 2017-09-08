#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "common/http/codec_client.h"
#include "common/network/filter_impl.h"
#include "common/stats/stats_impl.h"

#include "test/config/utility.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/server.h"
#include "test/integration/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"

#include "spdlog/spdlog.h"

namespace Envoy {
/**
 * Stream decoder wrapper used during integration testing.
 */
class IntegrationStreamDecoder : public Http::StreamDecoder, public Http::StreamCallbacks {
public:
  IntegrationStreamDecoder(Event::Dispatcher& dispatcher);

  const std::string& body() { return body_; }
  bool complete() { return saw_end_stream_; }
  bool reset() { return saw_reset_; }
  Http::StreamResetReason reset_reason() { return reset_reason_; }
  const Http::HeaderMap& headers() { return *headers_; }
  const Http::HeaderMapPtr& trailers() { return trailers_; }
  void waitForBodyData(uint64_t size);
  void waitForEndStream();
  void waitForReset();

  // Http::StreamDecoder
  void decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeTrailers(Http::HeaderMapPtr&& trailers) override;

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  Event::Dispatcher& dispatcher_;
  Http::HeaderMapPtr headers_;
  Http::HeaderMapPtr trailers_;
  bool waiting_for_end_stream_{};
  bool saw_end_stream_{};
  std::string body_;
  uint64_t body_data_waiting_length_{};
  bool waiting_for_reset_{};
  bool saw_reset_{};
  Http::StreamResetReason reset_reason_{};
};

typedef std::unique_ptr<IntegrationStreamDecoder> IntegrationStreamDecoderPtr;

/**
 * HTTP codec client used during integration testing.
 */
class IntegrationCodecClient : public Http::CodecClientProd {
public:
  IntegrationCodecClient(Event::Dispatcher& dispatcher, Network::ClientConnectionPtr&& conn,
                         Upstream::HostDescriptionConstSharedPtr host_description,
                         Http::CodecClient::Type type);

  void makeHeaderOnlyRequest(const Http::HeaderMap& headers, IntegrationStreamDecoder& response);
  void makeRequestWithBody(const Http::HeaderMap& headers, uint64_t body_size,
                           IntegrationStreamDecoder& response);
  bool sawGoAway() { return saw_goaway_; }
  void sendData(Http::StreamEncoder& encoder, Buffer::Instance& data, bool end_stream);
  void sendData(Http::StreamEncoder& encoder, uint64_t size, bool end_stream);
  void sendTrailers(Http::StreamEncoder& encoder, const Http::HeaderMap& trailers);
  void sendReset(Http::StreamEncoder& encoder);
  Http::StreamEncoder& startRequest(const Http::HeaderMap& headers,
                                    IntegrationStreamDecoder& response);
  void waitForDisconnect();

private:
  struct ConnectionCallbacks : public Network::ConnectionCallbacks {
    ConnectionCallbacks(IntegrationCodecClient& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    IntegrationCodecClient& parent_;
  };

  struct CodecCallbacks : public Http::ConnectionCallbacks {
    CodecCallbacks(IntegrationCodecClient& parent) : parent_(parent) {}

    // Http::ConnectionCallbacks
    void onGoAway() override { parent_.saw_goaway_ = true; }

    IntegrationCodecClient& parent_;
  };

  void flushWrite();

  ConnectionCallbacks callbacks_;
  CodecCallbacks codec_callbacks_;
  bool connected_{};
  bool disconnected_{};
  bool saw_goaway_{};
};

typedef std::unique_ptr<IntegrationCodecClient> IntegrationCodecClientPtr;

/**
 * TCP client used during integration testing.
 */
class IntegrationTcpClient {
public:
  IntegrationTcpClient(Event::Dispatcher& dispatcher, MockBufferFactory& factory, uint32_t port,
                       Network::Address::IpVersion version);

  void close();
  void waitForData(const std::string& data);
  void waitForDisconnect();
  void write(const std::string& data);
  const std::string& data() { return payload_reader_->data(); }

private:
  struct ConnectionCallbacks : public Network::ConnectionCallbacks {
    ConnectionCallbacks(IntegrationTcpClient& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    IntegrationTcpClient& parent_;
  };

  std::shared_ptr<WaitForPayloadReader> payload_reader_;
  std::shared_ptr<ConnectionCallbacks> callbacks_;
  Network::ClientConnectionPtr connection_;
  bool disconnected_{};
  MockWatermarkBuffer* client_write_buffer_;
};

typedef std::unique_ptr<IntegrationTcpClient> IntegrationTcpClientPtr;

struct ApiFilesystemConfig {
  std::string bootstrap_path_;
  std::string cds_path_;
  std::string eds_path_;
  std::string lds_path_;
  std::string rds_path_;
};

/**
 * Test fixture for all integration tests.
 */
class BaseIntegrationTest : Logger::Loggable<Logger::Id::testing> {
public:
  BaseIntegrationTest(Network::Address::IpVersion version);
  /**
   * Integration tests are composed of a sequence of actions which are run via this routine.
   */
  void executeActions(std::list<std::function<void()>> actions) {
    for (const std::function<void()>& action : actions) {
      action();
    }
  }

  Network::ClientConnectionPtr makeClientConnection(uint32_t port);

  IntegrationCodecClientPtr makeHttpConnection(uint32_t port, Http::CodecClient::Type type);
  IntegrationCodecClientPtr makeHttpConnection(Network::ClientConnectionPtr&& conn,
                                               Http::CodecClient::Type type);
  IntegrationTcpClientPtr makeTcpConnection(uint32_t port);

  // Test-wide port map.
  void registerPort(const std::string& key, uint32_t port);
  uint32_t lookupPort(const std::string& key);

  void sendRawHttpAndWaitForResponse(const char* http, std::string* response);
  void registerTestServerPorts(const std::vector<std::string>& port_names);
  void createTestServer(const std::string& json_path, const std::vector<std::string>& port_names);
  void createGeneratedApiTestServer(const std::string& bootstrap_path,
                                    const std::vector<std::string>& port_names);
  void createApiTestServer(const ApiFilesystemConfig& api_filesystem_config,
                           const std::vector<std::string>& port_names);

  Api::ApiPtr api_;
  MockBufferFactory* mock_buffer_factory_; // Will point to the dispatcher's factory.
  Event::DispatcherPtr dispatcher_;

protected:
  // Sends |request_headers| and |request_body_size| bytes of body upstream.
  // Configured upstream to send |response_headers| and |response_body_size|
  // bytes of body downstream.
  //
  // Waits for the complete downstream response before returning.
  // Requires |codec_client_| to be initialized.
  void sendRequestAndWaitForResponse(Http::TestHeaderMapImpl& request_headers,
                                     uint32_t request_body_size,
                                     Http::TestHeaderMapImpl& response_headers,
                                     uint32_t response_body_size);

  // Wait for the end of stream on the next upstream stream on fake_upstreams_
  // Sets fake_upstream_connection_ to the connection and upstream_request_ to stream.
  void waitForNextUpstreamRequest();

  // Close |codec_client_| and |fake_upstream_connection_| cleanly.
  void cleanupUpstreamAndDownstream();

  void testRouterRedirect(Http::CodecClient::Type type);
  void testRouterNotFound(Http::CodecClient::Type type);
  void testRouterNotFoundWithBody(uint32_t port, Http::CodecClient::Type type);
  void testRouterRequestAndResponseWithBody(Network::ClientConnectionPtr&& conn,
                                            Http::CodecClient::Type type, uint64_t request_size,
                                            uint64_t response_size, bool big_header);
  void testRouterHeaderOnlyRequestAndResponse(Network::ClientConnectionPtr&& conn,
                                              Http::CodecClient::Type type, bool close_upstream);
  void testRouterUpstreamDisconnectBeforeRequestComplete(Network::ClientConnectionPtr&& conn,
                                                         Http::CodecClient::Type type);
  void testRouterUpstreamDisconnectBeforeResponseComplete(Network::ClientConnectionPtr&& conn,
                                                          Http::CodecClient::Type type);
  void testRouterDownstreamDisconnectBeforeRequestComplete(Network::ClientConnectionPtr&& conn,
                                                           Http::CodecClient::Type type);
  void testRouterDownstreamDisconnectBeforeResponseComplete(Network::ClientConnectionPtr&& conn,
                                                            Http::CodecClient::Type type);
  void testRouterUpstreamResponseBeforeRequestComplete(Network::ClientConnectionPtr&& conn,
                                                       Http::CodecClient::Type type);
  void testTwoRequests(Http::CodecClient::Type type);
  void testBadFirstline();
  void testMissingDelimiter();
  void testInvalidCharacterInFirstline();
  void testLowVersion();
  void testHttp10Request();
  void testNoHost();
  void testOverlyLongHeaders(Http::CodecClient::Type type);
  void testUpstreamProtocolError();
  void testBadPath();
  void testAbsolutePath();
  void testAbsolutePathWithPort();
  void testAbsolutePathWithoutPort();
  void testConnect();
  void testAllowAbsoluteSameRelative();
  // Test that a request returns the same content with both allow_absolute_urls enabled and
  // allow_absolute_urls disabled
  void testEquivalent(const std::string& request);
  void testValidZeroLengthContent(Http::CodecClient::Type type);
  void testInvalidContentLength(Http::CodecClient::Type type);
  void testMultipleContentLengths(Http::CodecClient::Type type);
  void testDrainClose(Http::CodecClient::Type type);
  void testRetry(Http::CodecClient::Type type);
  void testGrpcRetry();

  // HTTP/2 client tests.
  void testDownstreamResetBeforeResponseComplete();
  void testTrailers(uint64_t request_size, uint64_t response_size);

  // The client making requests to Envoy.
  IntegrationCodecClientPtr codec_client_;
  // A placeholder for the first upstream connection.
  FakeHttpConnectionPtr fake_upstream_connection_;
  // A placeholder for the first response received by the client.
  IntegrationStreamDecoderPtr response_{new IntegrationStreamDecoder(*dispatcher_)};
  // A placeholder for the first request received at upstream.
  FakeStreamPtr upstream_request_;
  // A pointer to the request encoder, if used.
  Http::StreamEncoder* request_encoder_{nullptr};
  // The response headers sent by sendRequestAndWaitForResponse() by default.
  Http::TestHeaderMapImpl default_response_headers_{{":status", "200"}};
  // The IpVersion (IPv4, IPv6) to use.
  Network::Address::IpVersion version_;
  // The config for envoy start-up.
  ConfigHelper config_helper_{version_};
  std::vector<std::unique_ptr<FakeUpstream>> fake_upstreams_;
  spdlog::level::level_enum default_log_level_;
  IntegrationTestServerPtr test_server_;
  TestEnvironment::PortMap port_map_;
};
} // namespace Envoy
