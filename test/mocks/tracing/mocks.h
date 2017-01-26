#pragma once

#include "envoy/tracing/http_tracer.h"

namespace Tracing {

class MockConfig : public Config {
public:
  MockConfig();
  ~MockConfig();

  MOCK_CONST_METHOD0(operationName, const std::string&());
};

class MockSpan : public Span {
public:
  MockSpan();
  ~MockSpan();

  MOCK_METHOD2(setTag, void(const std::string& name, const std::string& value));
  MOCK_METHOD0(finishSpan, void());
};

class MockHttpTracer : public HttpTracer {
public:
  MockHttpTracer();
  ~MockHttpTracer();

  SpanPtr startSpan(const Config& config, const Http::HeaderMap& request_headers,
                    const Http::AccessLog::RequestInfo& request_info) override {
    return SpanPtr{startSpan_(config, request_headers, request_info)};
  }

  MOCK_METHOD3(startSpan_, Span*(const Config& config, const Http::HeaderMap& request_headers,
                                 const Http::AccessLog::RequestInfo& request_info));
};

class MockDriver : public Driver {
public:
  MockDriver();
  ~MockDriver();

  SpanPtr startSpan(const std::string& operation_name, SystemTime start_time) override {
    return SpanPtr{startSpan_(operation_name, start_time)};
  }

  MOCK_METHOD2(startSpan_, Span*(const std::string& operation_name, SystemTime start_time));
};

} // Tracing