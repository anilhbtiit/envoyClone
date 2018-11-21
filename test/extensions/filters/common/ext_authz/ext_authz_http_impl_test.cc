#include "envoy/api/v2/core/base.pb.h"

#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/protobuf/protobuf.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/filters/common/ext_authz/ext_authz_http_impl.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/common/ext_authz/test_common.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AllOf;
using testing::Invoke;
using testing::Ref;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::WhenDynamicCastTo;
using testing::WithArg;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

typedef std::vector<envoy::api::v2::core::HeaderValueOption> HeaderValueOptionVector;

class ExtAuthzHttpClientTest : public testing::Test {
public:
  ExtAuthzHttpClientTest()
      : cluster_name_{"foo"}, cluster_manager_{}, timeout_{}, path_prefix_{"/bar"},
        allowed_request_headers_{Http::LowerCaseString{":method"}, Http::LowerCaseString{":path"}},
        allowed_request_headers_prefix_{Http::LowerCaseString{"x-"}},
        authorization_headers_to_add_{}, allowed_upstream_headers_{Http::LowerCaseString{"bar"}},
        allowed_client_headers_{Http::LowerCaseString{"foo"}, Http::LowerCaseString{":status"}},
        async_client_{}, async_request_{&async_client_},
        client_(cluster_manager_, cluster_name_, timeout_, path_prefix_, allowed_request_headers_,
                allowed_request_headers_prefix_, authorization_headers_to_add_,
                allowed_upstream_headers_, allowed_client_headers_) {
    ON_CALL(cluster_manager_, httpAsyncClientForCluster(cluster_name_))
        .WillByDefault(ReturnRef(async_client_));
  }

  Http::MessagePtr sendRequest(std::unordered_map<std::string, std::string>&& headers) {
    envoy::service::auth::v2alpha::CheckRequest request{};
    auto mutable_headers =
        request.mutable_attributes()->mutable_request()->mutable_http()->mutable_headers();
    for (const auto& header : headers) {
      (*mutable_headers)[header.first] = header.second;
    }

    Http::MessagePtr message_ptr;
    EXPECT_CALL(async_client_, send_(_, _, _))
        .WillOnce(Invoke(
            [&](Http::MessagePtr& message, Http::AsyncClient::Callbacks&,
                const Envoy::Http::AsyncClient::RequestOptions) -> Http::AsyncClient::Request* {
              message_ptr = std::move(message);
              return nullptr;
            }));

    const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "200", false}});
    const auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::OK);
    auto check_response = TestCommon::makeMessageResponse(expected_headers);

    client_.check(request_callbacks_, request, Tracing::NullSpan::instance());
    EXPECT_CALL(request_callbacks_,
                onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
    client_.onSuccess(std::move(check_response));
    return message_ptr;
  }

  std::string cluster_name_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  MockRequestCallbacks request_callbacks_;
  absl::optional<std::chrono::milliseconds> timeout_;
  std::string path_prefix_;
  Http::LowerCaseStrUnorderedSet allowed_request_headers_;
  Http::LowerCaseStrUnorderedSet allowed_request_headers_prefix_;
  Http::LowerCaseStrPairVector authorization_headers_to_add_;
  Http::LowerCaseStrUnorderedSet allowed_upstream_headers_;
  Http::LowerCaseStrUnorderedSet allowed_client_headers_;
  NiceMock<Http::MockAsyncClient> async_client_;
  NiceMock<Http::MockAsyncClientRequest> async_request_;
  RawHttpClientImpl client_;
};

// Test the client when a request contains path to be re-written and ok response is received.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithPathRewrite) {
  Http::MessagePtr message_ptr = sendRequest({{":path", "/foo"}, {"foo", "bar"}});

  const auto* path = message_ptr->headers().get(Http::Headers::get().Path);
  ASSERT_NE(path, nullptr);
  EXPECT_EQ(path->value().getStringView(), "/bar/foo");
}

// Test the client when a request contains Content-Length greater than 0.
TEST_F(ExtAuthzHttpClientTest, ContentLengthEqualZero) {
  Http::MessagePtr message_ptr =
      sendRequest({{Http::Headers::get().ContentLength.get(), std::string{"47"}},
                   {Http::Headers::get().Method.get(), std::string{"POST"}}});

  const auto* content_length = message_ptr->headers().get(Http::Headers::get().ContentLength);
  ASSERT_NE(content_length, nullptr);
  EXPECT_EQ(content_length->value().getStringView(), "0");

  const auto* method = message_ptr->headers().get(Http::Headers::get().Method);
  ASSERT_NE(method, nullptr);
  EXPECT_EQ(method->value().getStringView(), "POST");
}

// Test the client when a request contains headers in the prefix whitelist.
TEST_F(ExtAuthzHttpClientTest, AllowedRequestHeadersPrefix) {
  allowed_request_headers_.emplace(Http::Headers::get().XContentTypeOptions.get());
  allowed_request_headers_prefix_.emplace(Http::Headers::get().XContentTypeOptions.get());

  Http::MessagePtr message_ptr =
      sendRequest({{Http::Headers::get().XContentTypeOptions.get(), "foobar"},
                   {Http::Headers::get().XSquashDebug.get(), "foo"},
                   {Http::Headers::get().ContentType.get(), "bar"}});

  EXPECT_EQ(message_ptr->headers().get(Http::Headers::get().ContentType), nullptr);
  const auto* x_squash = message_ptr->headers().get(Http::Headers::get().XSquashDebug);
  ASSERT_NE(x_squash, nullptr);
  EXPECT_EQ(x_squash->value().getStringView(), "foo");

  const auto* x_content_type = message_ptr->headers().get(Http::Headers::get().XContentTypeOptions);
  ASSERT_NE(x_content_type, nullptr);
  EXPECT_EQ(x_content_type->value().getStringView(), "foobar");
}

// Test the client when an ok response is received.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOk) {
  const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "200", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::OK);
  auto check_response = TestCommon::makeMessageResponse(expected_headers);
  envoy::service::auth::v2alpha::CheckRequest request;

  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));

  client_.onSuccess(std::move(check_response));
}

// Test the client when authorization headers to add are specified.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithAddedAuthzHeaders) {
  auto header1 = std::make_pair(Http::LowerCaseString("x-authz-header1"), "value");
  auto header2 = std::make_pair(Http::LowerCaseString("x-authz-header2"), "value");
  authorization_headers_to_add_.push_back(header1);
  authorization_headers_to_add_.push_back(header2);
  allowed_request_headers_.insert(header2.first);

  const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "200", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::OK);
  auto check_response = TestCommon::makeMessageResponse(expected_headers);
  envoy::service::auth::v2alpha::CheckRequest request;
  auto mutable_headers =
      request.mutable_attributes()->mutable_request()->mutable_http()->mutable_headers();
  (*mutable_headers)[std::string{":x-authz-header2"}] = std::string{"forged-value"};

  // Expect that header1 will be added and header2 correctly overwritten.
  EXPECT_CALL(async_client_,
              send_(AllOf(ContainsPairAsHeader(header1), ContainsPairAsHeader(header2)), _, _));
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  client_.onSuccess(std::move(check_response));
}

// Test that the client allows only header in the whitelist to be sent to the upstream.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithAllowHeader) {
  const std::string empty_body{};
  const auto expected_headers = TestCommon::makeHeaderValueOption({{"bar", "foo", false}});
  const auto authz_response =
      TestCommon::makeAuthzResponse(CheckStatus::OK, Http::Code::OK, empty_body, expected_headers);

  envoy::service::auth::v2alpha::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));

  const auto check_response_headers =
      TestCommon::makeHeaderValueOption({{":status", "200", false},
                                         {":path", "/bar", false},
                                         {":method", "post", false},
                                         {"content-length", "post", false},
                                         {"bar", "foo", false},
                                         {"foobar", "foo", false}});
  auto message_response = TestCommon::makeMessageResponse(check_response_headers);
  client_.onSuccess(std::move(message_response));
}

// Test the client when a denied response is received.
TEST_F(ExtAuthzHttpClientTest, AuthorizationDenied) {
  const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "403", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Denied, Http::Code::Forbidden, "", expected_headers);

  envoy::service::auth::v2alpha::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));

  client_.onSuccess(TestCommon::makeMessageResponse(expected_headers));
}

// Test the client when a denied response is received and it contains additional HTTP attributes.
TEST_F(ExtAuthzHttpClientTest, AuthorizationDeniedWithAllAttributes) {
  allowed_client_headers_.clear();
  const auto expected_body = std::string{"test"};
  const auto expected_headers = TestCommon::makeHeaderValueOption(
      {{":status", "401", false}, {"foo", "bar", false}, {"foobar", "bar", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Denied, Http::Code::Unauthorized, expected_body, expected_headers);

  envoy::service::auth::v2alpha::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));

  client_.onSuccess(TestCommon::makeMessageResponse(expected_headers, expected_body));
}

// Test the client when a denied response is received and allowed client headers is not empty.
TEST_F(ExtAuthzHttpClientTest, AuthorizationDeniedAndAllowedClientHeaders) {
  const auto expected_body = std::string{"test"};
  const auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Denied, Http::Code::Unauthorized, expected_body,
      TestCommon::makeHeaderValueOption({{":status", "401", false}, {"foo", "bar", false}}));

  envoy::service::auth::v2alpha::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));

  const auto check_response_headers = TestCommon::makeHeaderValueOption(
      {{"foo", "bar", false}, {"foobar", "bar", false}, {":status", "401", false}});
  client_.onSuccess(TestCommon::makeMessageResponse(check_response_headers, expected_body));
}

// Test the client when an unknown error occurs.
TEST_F(ExtAuthzHttpClientTest, AuthorizationRequestError) {
  envoy::service::auth::v2alpha::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_.onFailure(Http::AsyncClient::FailureReason::Reset);
}

// Test the client when a call to authorization server returns a 5xx error status.
TEST_F(ExtAuthzHttpClientTest, AuthorizationRequest5xxError) {
  Http::MessagePtr check_response(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "503"}}}));
  envoy::service::auth::v2alpha::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));

  client_.onSuccess(std::move(check_response));
}

// Test the client when a call to authorization server returns a status code that cannot be parsed.
TEST_F(ExtAuthzHttpClientTest, AuthorizationRequestErrorParsingStatusCode) {
  Http::MessagePtr check_response(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "foo"}}}));
  envoy::service::auth::v2alpha::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));

  client_.onSuccess(std::move(check_response));
}

// Test the client when the request is canceled.
TEST_F(ExtAuthzHttpClientTest, CancelledAuthorizationRequest) {
  envoy::service::auth::v2alpha::CheckRequest request;
  EXPECT_CALL(async_client_, send_(_, _, _)).WillOnce(Return(&async_request_));
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(async_request_, cancel());
  client_.cancel();
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
