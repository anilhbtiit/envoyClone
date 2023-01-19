import Envoy
import EnvoyEngine
import Foundation
import TestExtensions
import XCTest

final class SendDataTests: XCTestCase {
  override static func setUp() {
    super.setUp()
    register_test_extensions()
  }

  func testSendData() throws {
    // swiftlint:disable:next line_length
    let emhcmType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager"
    // swiftlint:disable:next line_length
    let assertionFilterType = "type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion"
    let requestStringMatch = "match_me"
    let config =
"""
static_resources:
  listeners:
  - name: base_api_listener
    address:
      socket_address:
        protocol: TCP
        address: 0.0.0.0
        port_value: 10000
    api_listener:
      api_listener:
        "@type": \(emhcmType)
        config:
          stat_prefix: hcm
          route_config:
            name: api_router
            virtual_hosts:
              - name: api
                domains:
                  - "*"
                routes:
                  - match:
                      prefix: "/"
                    direct_response:
                      status: 200
          http_filters:
            - name: envoy.filters.http.assertion
              typed_config:
                "@type": \(assertionFilterType)
                match_config:
                  http_request_generic_body_match:
                    patterns:
                      - string_match: \(requestStringMatch)
            - name: envoy.filters.http.buffer
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
                max_request_bytes: 65000
            - name: envoy.router
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
"""
    let expectation = self.expectation(description: "Run called with expected http status")
    let engine = EngineBuilder(yaml: config)
      .addLogLevel(.debug)
      .build()

    let client = engine.streamClient()

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "https",
                                               authority: "example.com", path: "/test")
      .addUpstreamHttpProtocol(.http2)
      .build()
    let body = try XCTUnwrap(requestStringMatch.data(using: .utf8))

    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         XCTAssertTrue(endStream)
         expectation.fulfill()
      }
      .setOnError { _, _ in
        XCTFail("Unexpected error")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: false)
      .close(data: body)

    XCTAssertEqual(XCTWaiter.wait(for: [expectation], timeout: 10), .completed)

    engine.terminate()
  }
}
