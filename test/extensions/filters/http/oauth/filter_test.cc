#include <memory>
#include <string>

#include "envoy/extensions/filters/http/oauth/v3/oauth.pb.h"
#include "envoy/http/async_client.h"
#include "envoy/http/message.h"

#include "common/http/message_impl.h"

#include "extensions/filters/http/oauth/filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth {

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

static const std::string TEST_CALLBACK = "/_oauth";
static const std::string TEST_CLIENT_ID = "1";
static const std::string TEST_CLIENT_SECRET_ID = "MyClientSecretKnoxID";
static const std::string TEST_TOKEN_SECRET_ID = "MyTokenSecretKnoxID";

namespace {
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    authorization_handle(Http::CustomHeaders::get().Authorization);
}

class MockSecretReader : public SecretReader {
public:
  MockSecretReader() = default;
  std::string clientSecret() const override { return "asdf_client_secret_fdsa"; }
  std::string tokenSecret() const override { return "asdf_token_secret_fdsa"; }
};

class MockOAuth2CookieValidator : public CookieValidator {
public:
  MockOAuth2CookieValidator() = default;
  ~MockOAuth2CookieValidator() override = default;
  MOCK_METHOD(std::string&, username, (), (const));
  MOCK_METHOD(std::string&, token, (), (const));
  MOCK_METHOD(bool, isValid, (), (const));
  MOCK_METHOD(void, setParams, (const Http::RequestHeaderMap& headers, const std::string& secret));
};

class MockOAuth2Client : public OAuth2Client {
public:
  MockOAuth2Client() = default;
  ~MockOAuth2Client() override = default;

  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override {}
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override {}
  void setCallbacks(OAuth2FilterCallbacks&) override {}
  void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                    const Http::ResponseHeaderMap*) override {}

  MOCK_METHOD(void, asyncGetAccessToken,
              (const std::string&, const std::string&, const std::string&, const std::string&));
};

class OAuth2Test : public testing::Test {
public:
  OAuth2Test() : request_(&cm_.async_client_) {
    // Set up the OAuth client
    oauth_client_ = new MockOAuth2Client();
    std::unique_ptr<OAuth2Client> oauth_client_ptr{oauth_client_};

    // Set up proto fields
    envoy::extensions::filters::http::oauth::v3::OAuth2Config p;
    p.set_cluster("auth.example.com");
    p.set_hostname("auth.example.com");
    p.set_callback_path(TEST_CALLBACK);
    p.set_signout_path("/_signout");
    p.set_forward_bearer_token(true);
    p.set_pass_through_options_method(true);
    p.mutable_credentials()->set_client_id(TEST_CLIENT_ID);

    // Create the OAuth config.
    auto secret_reader = std::make_shared<MockSecretReader>();
    config_ = std::make_shared<FilterConfig>(p, factory_context_.cluster_manager_, secret_reader,
                                             scope_, "test.");

    filter_ = std::make_shared<OAuth2Filter>(config_, std::move(oauth_client_ptr), test_time_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    validator_ = std::make_shared<MockOAuth2CookieValidator>();
    filter_->validator_ = validator_;
  }

  Http::AsyncClient::Callbacks* popPendingCallback() {
    if (callbacks_.empty()) {
      // Can't use ASSERT_* as this is not a test function
      throw std::underflow_error("empty deque");
    }

    auto callbacks = callbacks_.front();
    callbacks_.pop_front();
    return callbacks;
  }

  NiceMock<Event::MockTimer>* attachmentTimeout_timer_{};
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Upstream::MockClusterManager> cm_;
  std::shared_ptr<MockOAuth2CookieValidator> validator_;
  std::shared_ptr<OAuth2Filter> filter_;
  MockOAuth2Client* oauth_client_;
  FilterConfigSharedPtr config_;
  Http::MockAsyncClientRequest request_;
  std::deque<Http::AsyncClient::Callbacks*> callbacks_;
  Stats::IsolatedStoreImpl scope_;
  Event::SimulatedTimeSystem test_time_;
};

/**
 * Scenario: The OAuth filter receives a sign out request.
 *
 * Expected behavior: the filter should redirect to the server name with cleared OAuth cookies.
 */
TEST_F(OAuth2Test, RequestSignout) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().ForwardedProto.get(), "https"},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().Location.get(), "https://traffic.example.com/"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: The OAuth filter receives a request to an arbitrary path with valid OAuth cookies
 * (cookie values and validation are mocked out)
 * In a real flow, the injected OAuth headers should be sanitized and replaced with legitimate
 * values.
 *
 * Expected behavior: the filter should let the request proceed, and sanitize the injected headers.
 */
TEST_F(OAuth2Test, OAuthOkPass) {
  Http::TestRequestHeaderMapImpl mock_request_headers{
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().ForwardedProto.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer injected_malice!"},
  };

  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().ForwardedProto.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer legit_token"},
  };

  // cookie-validation mocking
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  // Sanitized return reference mocking
  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillOnce(ReturnRef(legit_token));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(mock_request_headers, false));

  // Ensure that existing OAuth forwarded headers got sanitized.
  EXPECT_EQ(mock_request_headers, expected_headers);

  EXPECT_EQ(scope_.counterFromString("test.oauth_failure").value(), 0);
  EXPECT_EQ(scope_.counterFromString("test.oauth_success").value(), 1);
}

/**
 * Scenario: The OAuth filter receives a request without valid OAuth cookies to a non-callback URL
 * (indicating that the user needs to re-validate cookies or get 401'd).
 * This also tests both a forwarded http protocol from upstream and a plaintext connection.
 *
 * Expected behavior: the filter should redirect the user to the OAuth server with the credentials
 * in the query parameters.
 */
TEST_F(OAuth2Test, OAuthErrorNonOAuthHttpCallback) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/not/_oauth"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "http"},
      {Http::Headers::get().ForwardedProto.get(), "http"},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID +
           "&scope=user&response_type=code&"
           "redirect_uri=http%3A%2F%2Ftraffic.example.com%2F"
           "_oauth&state=http%3A%2F%2Ftraffic.example.com%2Fnot%2F_oauth"},
  };

  // explicitly tell the validator to fail the validation
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: The OAuth filter receives a callback request with an error code
 */
TEST_F(OAuth2Test, OAuthErrorQueryString) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?error=someerrorcode"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "401"},
      {Http::Headers::get().ContentLength.get(), "18"}, // unauthorizedBodyMessage()
      {Http::Headers::get().ContentType.get(), "text/plain"},
  };

  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(request_headers, false));

  EXPECT_EQ(scope_.counterFromString("test.oauth_failure").value(), 1);
  EXPECT_EQ(scope_.counterFromString("test.oauth_success").value(), 0);
}

/**
 * Scenario: The OAuth filter requests credentials from auth.example.com which returns a
 * response without expires_in (JSON response is mocked out)
 *
 * Expected behavior: the filter should return a 401 directly to the user.
 */
TEST_F(OAuth2Test, OAuthCallbackStartsAuthentication) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=https://asdf&method=GET"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  // Deliberately fail the HMAC Validation check.
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(*oauth_client_, asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                                  "https://traffic.example.com" + TEST_CALLBACK));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: Protoc in opted-in to allow OPTIONS requests to pass-through. This is important as
 * POST requests initiate an OPTIONS request first in order to ensure POST is supported. During a
 * preflight request where the client Javascript initiates a remote call to a different endpoint,
 * we don't want to fail the call immediately due to browser restrictions, and use existing
 * cookies instead (OPTIONS requests do not send OAuth cookies.)
 */
TEST_F(OAuth2Test, OAuthOptionsRequestAndContinue) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Options},
  };

  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: The OAuth filter has successfully set the cookie parameters in the headers and
 * we should expect to continue to the next filter in the chain after validating HMAC/expiration.
 * This infers forward_bearer_token = true because we provide a BearerToken cookie to the validator.
 * Otherwise, this cookie should be void and will not contribute to the HMAC validation.
 *
 * Expected behavior: the OAuth2CookieValidator should return true after parsing the cookie values.
 */
TEST_F(OAuth2Test, OAuthValidatedCookieAndContinue) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(), "OauthExpires=123;version=test"},
      {Http::Headers::get().Cookie.get(), "BearerToken=xyztoken;version=test"},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "NmU4ZjFjMWNkYzQwOTA5YzUwMmYwN2U1MDcxZjA2Y2VlNmZlODczNmRhYjA5ZjZiZGQ0ODVkNjAzODljYmM0NA=="
       ";version=test"},
  };

  auto cookie_validator = std::make_shared<OAuth2CookieValidator>(test_time_);
  cookie_validator->setParams(request_headers, "mock-secret");

  EXPECT_EQ(cookie_validator->hmacIsValid(), true);
}

/**
 * Testing the setXForwardedOauthHeaders function.
 *
 * Expected behavior: the current HeaderMap should reflect the newly added x-forwarded headers.
 */
TEST_F(OAuth2Test, OAuthTestSetOAuthHeaders) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(), "OauthExpires=123;version=test"},
      {Http::Headers::get().Cookie.get(), "BearerToken=xyztoken;version=test"},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "ZTRlMzU5N2Q4ZDIwZWE5ZTU5NTg3YTU3YTcxZTU0NDFkMzY1ZTc1NjMyODYyMj"
       "RlNjMxZTJmNTZkYzRmZTM0ZQ====;version=test"},
  };

  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(), "OauthExpires=123;version=test"},
      {Http::Headers::get().Cookie.get(), "BearerToken=xyztoken;version=test"},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "ZTRlMzU5N2Q4ZDIwZWE5ZTU5NTg3YTU3YTcxZTU0NDFkMzY1ZTc1NjMyODYyMj"
       "RlNjMxZTJmNTZkYzRmZTM0ZQ====;version=test"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer xyztoken"},
  };

  auto cookie_validator = std::make_shared<OAuth2CookieValidator>(test_time_);
  cookie_validator->setParams(request_headers, "mock-secret");
  filter_->setXForwardedOauthHeaders(request_headers, cookie_validator->token());

  EXPECT_EQ(request_headers, expected_headers);
}

/**
 * Testing the Path header replacement after an OAuth success.
 *
 * Expected behavior: the passed in HeaderMap should pass the OAuth flow, but since it's during
 * a callback from the authentication server, we should first parse out the state query string
 * parameter and set it to be the new path.
 */
TEST_F(OAuth2Test, OAuthTestUpdatePathAfterSuccess) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Path.get(), "/_oauth?code=abcdefxyz123&scope=user&"
                                        "state=https%3A%2F%2Ftraffic.example.com%2Foriginal_path"},
      {Http::Headers::get().Cookie.get(), "OauthExpires=123;version=test"},
      {Http::Headers::get().Cookie.get(), "BearerToken=legit_token;version=test"},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "ZTRlMzU5N2Q4ZDIwZWE5ZTU5NTg3YTU3YTcxZTU0NDFkMzY1ZTc1NjMyODYyMj"
       "RlNjMxZTJmNTZkYzRmZTM0ZQ====;version=test"},
  };

  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(), "https://traffic.example.com/original_path"},
  };

  // Succeed the HMAC validation.
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillOnce(ReturnRef(legit_token));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
}

/**
 * Testing oauth state with query string parameters.
 *
 * Expected behavior: HTTP Utility should not strip the parameters of the original request.
 */
TEST_F(OAuth2Test, OAuthTestFullFlowPostWithParameters) {
  // First construct the initial request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl first_request_headers{
      {Http::Headers::get().Path.get(), "/test?name=admin&level=trace"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().ForwardedProto.get(), "https"},
  };

  // This is the immediate response - a redirect to the auth cluster.
  Http::TestResponseHeaderMapImpl first_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID +
           "&scope=user&response_type=code&"
           "redirect_uri=https%3A%2F%2Ftraffic.example.com%2F"
           "_oauth&state=https%3A%2F%2Ftraffic.example.com%2Ftest%"
           "3Fname%3Dadmin%26level%3Dtrace"},
  };

  // Fail the validation to trigger the OAuth flow.
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  // Check that the redirect includes the escaped parameter characters, '?', '&' and '='.
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&first_response_headers), true));

  // This represents the beginning of the OAuth filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(first_request_headers, false));

  // This represents the callback request from the authorization server.
  Http::TestRequestHeaderMapImpl second_request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=https%3A%2F%2Ftraffic.example.com%"
                                        "2Ftest%3Fname%3Dadmin%26level%3Dtrace"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  // Deliberately fail the HMAC validation check.
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(*oauth_client_, asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                                  "https://traffic.example.com" + TEST_CALLBACK));

  // Invoke the callback logic. As a side effect, state_ will be populated.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(second_request_headers, false));

  // Expected response after the callback & validation is complete - verifying we kept the
  // state and method of the original request, including the query string parameters.
  Http::TestRequestHeaderMapImpl second_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC="
       "NWUzNzE5MWQwYTg0ZjA2NjIyMjVjMzk3MzY3MzMyZmE0NjZmMWI2MjI1NWFhNDhkYjQ4NDFlZmRiMTVmMTk0MQ==;"
       "version=1;path=/;Max-Age=;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=;version=1;path=/;Max-Age=;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(), "BearerToken=;version=1;path=/;Max-Age=;secure"},
      {Http::Headers::get().Location.get(),
       "https://traffic.example.com/test?name=admin&level=trace"},
  };

  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&second_response_headers), true));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  filter_->finishFlow();
}

TEST_F(OAuth2Test, OAuthBearerTokenFlowFromHeader) {
  Http::TestRequestHeaderMapImpl request_headers_before{
      {Http::Headers::get().Path.get(), "/test?role=bearer"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().ForwardedProto.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer xyz-header-token"},
  };
  // Expected decoded headers after the callback & validation of the bearer token is complete.
  Http::TestRequestHeaderMapImpl request_headers_after{
      {Http::Headers::get().Path.get(), "/test?role=bearer"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().ForwardedProto.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer xyz-header-token"},
  };

  // Fail the validation to trigger the OAuth flow.
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_before, false));

  // Finally, expect that the header map had OAuth information appended to it.
  EXPECT_EQ(request_headers_before, request_headers_after);
}

TEST_F(OAuth2Test, OAuthBearerTokenFlowFromQueryParameters) {
  Http::TestRequestHeaderMapImpl request_headers_before{
      {Http::Headers::get().Path.get(), "/test?role=bearer&token=xyz-queryparam-token"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().ForwardedProto.get(), "https"},
  };
  Http::TestRequestHeaderMapImpl request_headers_after{
      {Http::Headers::get().Path.get(), "/test?role=bearer&token=xyz-queryparam-token"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().ForwardedProto.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer xyz-queryparam-token"},
  };

  // Fail the validation to trigger the OAuth flow.
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_before, false));

  // Expected decoded headers after the callback & validation of the bearer token is complete.
  EXPECT_EQ(request_headers_before, request_headers_after);
}

} // namespace Oauth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
