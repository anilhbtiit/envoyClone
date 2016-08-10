#include "http2_upstream_integration_test.h"

#include "common/http/header_map_impl.h"

TEST_F(Http2UpstreamIntegrationTest, RouterNotFound) {
  testRouterNotFound(Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2UpstreamIntegrationTest, RouterRedirect) {
  testRouterRedirect(Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2UpstreamIntegrationTest, DrainClose) { testDrainClose(Http::CodecClient::Type::HTTP2); }

TEST_F(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(IntegrationTest::HTTP_PORT),
                                       Http::CodecClient::Type::HTTP2, 1024, 512);
}

TEST_F(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(IntegrationTest::HTTP_BUFFER_PORT),
                                       Http::CodecClient::Type::HTTP2, 1024, 512);
}

TEST_F(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithZeroByteBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(IntegrationTest::HTTP_PORT),
                                       Http::CodecClient::Type::HTTP2, 0, 0);
}

TEST_F(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithZeroByteBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(IntegrationTest::HTTP_BUFFER_PORT),
                                       Http::CodecClient::Type::HTTP2, 0, 0);
}

TEST_F(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithBodyHttp1) {
  testRouterRequestAndResponseWithBody(makeClientConnection(10004), Http::CodecClient::Type::HTTP1,
                                       1024, 512);
}

TEST_F(Http2UpstreamIntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(IntegrationTest::HTTP_PORT),
                                         Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2UpstreamIntegrationTest, RouterHeaderOnlyRequestAndResponseBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(IntegrationTest::HTTP_BUFFER_PORT),
                                         Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2UpstreamIntegrationTest, RouterHeaderOnlyRequestAndResponseHttp1) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(10004),
                                         Http::CodecClient::Type::HTTP1);
}

TEST_F(Http2UpstreamIntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete(
      makeClientConnection(IntegrationTest::HTTP_PORT), Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2UpstreamIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete(
      makeClientConnection(IntegrationTest::HTTP_PORT), Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2UpstreamIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete(
      makeClientConnection(IntegrationTest::HTTP_PORT), Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2UpstreamIntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete(
      makeClientConnection(IntegrationTest::HTTP_PORT), Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2UpstreamIntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete(makeClientConnection(IntegrationTest::HTTP_PORT),
                                                  Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2UpstreamIntegrationTest, TwoRequests) {
  testTwoRequests(Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2UpstreamIntegrationTest, Retry) { testRetry(Http::CodecClient::Type::HTTP2); }

TEST_F(Http2UpstreamIntegrationTest, DownstreamResetBeforeResponseComplete) {
  testDownstreamResetBeforeResponseComplete();
}

TEST_F(Http2UpstreamIntegrationTest, Trailers) { testTrailers(1024, 2048); }

TEST_F(Http2UpstreamIntegrationTest, SimultaneousRequest) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  Http::StreamEncoder* encoder1;
  Http::StreamEncoder* encoder2;
  IntegrationStreamDecoderPtr response1(new IntegrationStreamDecoder(*dispatcher_));
  IntegrationStreamDecoderPtr response2(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr upstream_request1;
  FakeStreamPtr upstream_request2;
  executeActions(
      {[&]() -> void {
        codec_client =
            makeHttpConnection(IntegrationTest::HTTP_PORT, Http::CodecClient::Type::HTTP2);
      },
       // Start request 1
       [&]() -> void {
         encoder1 = &codec_client->startRequest(Http::HeaderMapImpl{{":method", "POST"},
                                                                    {":path", "/test/long/url"},
                                                                    {":scheme", "http"},
                                                                    {":authority", "host"}},
                                                *response1);
       },
       [&]() -> void {
         fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { upstream_request1 = fake_upstream_connection->waitForNewStream(); },

       // Start request 2
       [&]() -> void {
         response2.reset(new IntegrationStreamDecoder(*dispatcher_));
         encoder2 = &codec_client->startRequest(Http::HeaderMapImpl{{":method", "POST"},
                                                                    {":path", "/test/long/url"},
                                                                    {":scheme", "http"},
                                                                    {":authority", "host"}},
                                                *response2);
       },
       [&]() -> void { upstream_request2 = fake_upstream_connection->waitForNewStream(); },

       // Finish request 1
       [&]() -> void {
         codec_client->sendData(*encoder1, 1024, true);

       },
       [&]() -> void { upstream_request1->waitForEndStream(*dispatcher_); },

       // Finish request 2
       [&]() -> void {
         codec_client->sendData(*encoder2, 512, true);

       },
       [&]() -> void { upstream_request2->waitForEndStream(*dispatcher_); },

       // Respond request 2
       [&]() -> void {
         upstream_request2->encodeHeaders(Http::HeaderMapImpl{{":status", "200"}}, false);
         upstream_request2->encodeData(1024, true);
       },
       [&]() -> void {
         response2->waitForEndStream();
         EXPECT_TRUE(upstream_request2->complete());
         EXPECT_EQ(512U, upstream_request2->bodyLength());

         EXPECT_TRUE(response2->complete());
         EXPECT_EQ("200", response2->headers().get(":status"));
         EXPECT_EQ(1024U, response2->body().size());
       },

       // Respond request 1
       [&]() -> void {
         upstream_request1->encodeHeaders(Http::HeaderMapImpl{{":status", "200"}}, false);
         upstream_request1->encodeData(512, true);
       },
       [&]() -> void {
         response1->waitForEndStream();
         EXPECT_TRUE(upstream_request1->complete());
         EXPECT_EQ(1024U, upstream_request1->bodyLength());

         EXPECT_TRUE(response1->complete());
         EXPECT_EQ("200", response1->headers().get(":status"));
         EXPECT_EQ(512U, response1->body().size());
       },

       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});
}
