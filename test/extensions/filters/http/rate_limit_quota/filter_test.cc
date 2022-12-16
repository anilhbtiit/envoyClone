#include <initializer_list>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"

#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/client.h"
#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/filter.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/rate_limit_quota/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {
namespace {

using Server::Configuration::MockFactoryContext;
using testing::Invoke;
using testing::Unused;

class RateLimitStreamUtility {
public:
  RateLimitStreamUtility() {
    grpc_service_.mutable_envoy_grpc()->set_cluster_name("rate_limit_quota");
    // Set the expected behavior for async_client_manager in mock context.
    // Note, we need to set it through `MockFactoryContext` rather than `MockAsyncClientManager`
    // directly because the rate limit client object below requires context argument as the input.
    EXPECT_CALL(context_.cluster_manager_.async_client_manager_, getOrCreateRawAsyncClient(_, _, _))
        .WillOnce(Invoke(this, &RateLimitStreamUtility::mockCreateAsyncClient));

    client_ = createRateLimitClient(context_, grpc_service_, &reports_);
  }

  Grpc::RawAsyncClientSharedPtr mockCreateAsyncClient(Unused, Unused, Unused) {
    auto async_client = std::make_shared<Grpc::MockAsyncClient>();
    EXPECT_CALL(*async_client, startRaw("envoy.service.rate_limit_quota.v3.RateLimitQuotaService",
                                        "StreamRateLimitQuotas", _, _))
        .WillOnce(Invoke(this, &RateLimitStreamUtility::mockStartRaw));

    return async_client;
  }

  Grpc::RawAsyncStream* mockStartRaw(Unused, Unused, Grpc::RawAsyncStreamCallbacks& callbacks,
                                     const Http::AsyncClient::StreamOptions&) {
    stream_callbacks_ = &callbacks;
    return &stream_;
  }

  ~RateLimitStreamUtility() = default;

  NiceMock<MockFactoryContext> context_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;

  envoy::config::core::v3::GrpcService grpc_service_;
  Grpc::MockAsyncStream stream_;
  Grpc::RawAsyncStreamCallbacks* stream_callbacks_;
  Grpc::Status::GrpcStatus grpc_status_ = Grpc::Status::WellKnownGrpcStatus::Ok;
  RateLimitClientPtr client_;
  MockRateLimitQuotaCallbacks callbacks_;
  RateLimitQuotaUsageReports reports_;

  bool grpc_closed_ = false;
};

using ::Envoy::Extensions::HttpFilters::RateLimitQuota::FilterConfig;
using ::Envoy::StatusHelpers::StatusIs;
using Server::Configuration::MockFactoryContext;
using ::testing::NiceMock;

constexpr char ValidMatcherConfig[] = R"EOF(
  matcher_list:
    matchers:
      # Assign requests with header['env'] set to 'staging' to the bucket { name: 'staging' }
      predicate:
        single_predicate:
          input:
            typed_config:
              "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
              header_name: environment
          value_match:
            exact: staging
      on_match:
        action:
          name: rate_limit_quota
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
            bucket_id_builder:
              bucket_id_builder:
                "name":
                    string_value: "prod"
                "environment":
                    custom_value:
                      name: "test_1"
                      typed_config:
                        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                        header_name: environment
                "group":
                    custom_value:
                      name: "test_2"
                      typed_config:
                        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                        header_name: group
            reporting_interval: 60s
            no_assignment_behavior:
              fallback_rate_limit:
                blanket_rule: ALLOW_ALL
  )EOF";

constexpr char OnNoMatchConfig[] = R"EOF(
  matcher_list:
    matchers:
      predicate:
        single_predicate:
          input:
            typed_config:
              "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
              header_name: environment
          value_match:
            exact: staging
      # Here is on_match field that will not be matched by the request header.
      on_match:
        action:
          name: rate_limit_quota
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
            bucket_id_builder:
              bucket_id_builder:
                "NO_MATCHED_NAME":
                    string_value: "NO_MATCHED"
            reporting_interval: 60s
  on_no_match:
    action:
      name: rate_limit_quota
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
        bucket_id_builder:
          bucket_id_builder:
            "on_no_match_name":
                string_value: "on_no_match_value"
            "on_no_match_name_2":
                string_value: "on_no_match_value_2"
            # TODO(tyxia) The config below will hit the error "No matched result from custom value config."
            # because we don't have on_no_match action support.
            #"environment":
            #    custom_value:
            #      name: "test_1"
            #      typed_config:
            #        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
            #        header_name: environment
        deny_response_settings:
          grpc_status:
            code: 8
        expired_assignment_behavior:
          fallback_rate_limit:
            blanket_rule: ALLOW_ALL
        reporting_interval: 5s
)EOF";

// It uses Google Grpc config.
constexpr char FilterConfigStr[] = R"EOF(
  rlqs_server:
    google_grpc:
      target_uri: rate_limit_quota_server
      stat_prefix: google
  domain:
    rate_limit_quota_test
)EOF";

// const std::string GoogleGrpcConfig = R"EOF(
//   rlqs_server:
//     google_grpc:
//       target_uri: rate_limit_quota_server
//       stat_prefix: google
//   )EOF";

// const std::string GrpcConfig = R"EOF(
//   rlqs_server:
//     envoy_grpc:
//       cluster_name: "rate_limit_quota_server"
//   )EOF";

// TODO(tyxia) CEL matcher config to be used later.
// constexpr char CelMatcherConfig[] = R"EOF(
//     matcher_list:
//       matchers:
//         # Assign requests with header['env'] set to 'staging' to the bucket { name: 'staging' }
//         predicate:
//           single_predicate:
//             input:
//               typed_config:
//                 "@type": type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput
//                 header_name: environment
//             custom_match:
//               typed_config:
//                 '@type': type.googleapis.com/xds.type.matcher.v3.CelMatcher
//                 expr_match:
//                   # Shortened for illustration purposes. Here should be parsed CEL expression:
//                   # request.headers['user_group'] == 'admin'
//                   parsed_expr: {}
//         on_match:
//           action:
//             name: rate_limit_quota
//             typed_config:
//               "@type":
//               type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
//               bucket_id_builder:
//                 bucket_id_builder:
//                   "name":
//                       string_value: "prod"
//   )EOF";

enum class MatcherConfigType { Valid, Invalid, IncludeOnNoMatchConfig };

class FilterTest : public testing::Test {
public:
  FilterTest() {
    // Add the grpc service config.
    TestUtility::loadFromYaml(FilterConfigStr, config_);
    RateLimitStreamUtility utility;
    EXPECT_OK(utility.client_->startStream(utility.stream_info_));
  }

  ~FilterTest() override { filter_->onDestroy(); }

  void addMatcherConfig(MatcherConfigType config_type) {
    // Add the matcher configuration.
    switch (config_type) {
    case MatcherConfigType::Valid: {
      xds::type::matcher::v3::Matcher matcher;
      TestUtility::loadFromYaml(ValidMatcherConfig, matcher);
      config_.mutable_bucket_matchers()->MergeFrom(matcher);
      break;
    }
    case MatcherConfigType::IncludeOnNoMatchConfig: {
      xds::type::matcher::v3::Matcher matcher;
      TestUtility::loadFromYaml(OnNoMatchConfig, matcher);

      config_.mutable_bucket_matchers()->MergeFrom(matcher);
      break;
    }
    // Invalid bucket_matcher configuration will be just empty matcher config.
    case MatcherConfigType::Invalid:
    default:
      break;
    }
  }

  void createFilter(bool set_callback = true) {
    filter_config_ = std::make_shared<FilterConfig>(config_);
    filter_ = std::make_unique<RateLimitQuotaFilter>(filter_config_, context_, &bucket_cache_);
    if (set_callback) {
      filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    }
  }

  void constructMismatchedRequestHeader() {
    // Define the wrong input that doesn't match the values in the config: it has `{"env",
    // "staging"}` rather than `{"environment", "staging"}`.
    absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"env", "staging"},
                                                                        {"group", "envoy"}};

    // Add custom_value_pairs to the request header for exact value_match in the predicate.
    for (auto const& pair : custom_value_pairs) {
      default_headers_.addCopy(pair.first, pair.second);
    }
  }

  void buildCustomHeader(const absl::flat_hash_map<std::string, std::string>& custom_value_pairs) {
    // Add custom_value_pairs to the request header for exact value_match in the predicate.
    for (auto const& pair : custom_value_pairs) {
      default_headers_.addCopy(pair.first, pair.second);
    }
  }

  NiceMock<MockFactoryContext> context_;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;

  std::unique_ptr<RateLimitQuotaFilter> filter_;
  FilterConfigConstSharedPtr filter_config_;
  FilterConfig config_;
  Http::TestRequestHeaderMapImpl default_headers_{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  BucketContainer bucket_cache_;
};

TEST_F(FilterTest, InvalidBucketMatcherConfig) {
  addMatcherConfig(MatcherConfigType::Invalid);
  createFilter();
  auto match_result = filter_->requestMatching(default_headers_);
  EXPECT_FALSE(match_result.ok());
  EXPECT_THAT(match_result, StatusIs(absl::StatusCode::kInternal));
  EXPECT_EQ(match_result.status().message(), "Matcher tree has not been initialized yet");
}

TEST_F(FilterTest, RequestMatchingSucceeded) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();
  // Define the key value pairs that is used to build the bucket_id dynamically via `custom_value`
  // in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};

  buildCustomHeader(custom_value_pairs);

  // The expected bucket ids has one additional pair that is built statically via `string_value`
  // from the config.
  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({"name", "prod"});

  // Perform request matching.
  auto match_result = filter_->requestMatching(default_headers_);
  // Asserts that the request matching succeeded and then retrieve the matched action.
  ASSERT_TRUE(match_result.ok());
  const RateLimitOnMactchAction* match_action =
      dynamic_cast<RateLimitOnMactchAction*>(match_result.value().get());

  RateLimitQuotaValidationVisitor visitor = {};
  // Generate the bucket ids.
  auto ret = match_action->generateBucketId(filter_->matchingData(), context_, visitor);
  // Asserts that the bucket id generation succeeded and then retrieve the bucket ids.
  ASSERT_TRUE(ret.ok());
  auto bucket_ids = ret.value().bucket();

  // Serialize the proto map to std map for comparison. We can avoid this conversion by using
  // `EqualsProto()` directly once it is available in the Envoy code base.
  auto serialized_bucket_ids =
      absl::flat_hash_map<std::string, std::string>(bucket_ids.begin(), bucket_ids.end());

  EXPECT_THAT(expected_bucket_ids,
              testing::UnorderedPointwise(testing::Eq(), serialized_bucket_ids));

  envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse resp;
  filter_->onQuotaResponse(resp);
}

TEST_F(FilterTest, RequestMatchingFailed) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();
  constructMismatchedRequestHeader();

  // Perform request matching.
  auto match = filter_->requestMatching(default_headers_);
  // Not_OK status is expected to be returned because the matching failed due to mismatched inputs.
  EXPECT_FALSE(match.ok());
  EXPECT_THAT(match, StatusIs(absl::StatusCode::kNotFound));
  EXPECT_EQ(match.status().message(), "The match was completed, no match found");
}

TEST_F(FilterTest, RequestMatchingFailedWithNoCallback) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter(/*set_callback*/ false);

  auto match = filter_->requestMatching(default_headers_);
  EXPECT_FALSE(match.ok());
  EXPECT_THAT(match, StatusIs(absl::StatusCode::kInternal));
  EXPECT_EQ(match.status().message(), "Filter callback has not been initialized successfully yet.");
}

TEST_F(FilterTest, RequestMatchingFailedWithOnNoMatchConfigured) {
  addMatcherConfig(MatcherConfigType::IncludeOnNoMatchConfig);
  createFilter();
  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = {
      {"on_no_match_name", "on_no_match_value"}, {"on_no_match_name_2", "on_no_match_value_2"}};
  // Perform request matching.
  auto match_result = filter_->requestMatching(default_headers_);
  // Asserts that the request matching succeeded.
  // OK status is expected to be returned even if the exact request matching failed. It is because
  // `on_no_match` field is configured.
  ASSERT_TRUE(match_result.ok());
  // Retrieve the matched action.
  const RateLimitOnMactchAction* match_action =
      dynamic_cast<RateLimitOnMactchAction*>(match_result.value().get());

  RateLimitQuotaValidationVisitor visitor = {};
  // Generate the bucket ids.
  auto ret = match_action->generateBucketId(filter_->matchingData(), context_, visitor);
  // Asserts that the bucket id generation succeeded and then retrieve the bucket ids.
  ASSERT_TRUE(ret.ok());
  auto bucket_ids = ret.value().bucket();
  auto serialized_bucket_ids =
      absl::flat_hash_map<std::string, std::string>(bucket_ids.begin(), bucket_ids.end());
  // Verifies that the expected bucket ids are generated for `on_no_match` case.
  EXPECT_THAT(expected_bucket_ids,
              testing::UnorderedPointwise(testing::Eq(), serialized_bucket_ids));
}

// TODO(tyxia) This may need the integration test to start the fake grpc client
// TEST_F(FilterTest, DecodeHeaderWithValidConfig) {
//   addMatcherConfig(MatcherConfigType::Valid);
//   createFilter();

//   // Define the key value pairs that is used to build the bucket_id dynamically via
//   `custom_value`
//   // in the config.
//   absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
//                                                                       {"group", "envoy"}};

//   buildCustomHeader(custom_value_pairs);

//   Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
//   EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::Continue);
// }

TEST_F(FilterTest, DecodeHeaderWithOnNoMatchConfigured) {
  addMatcherConfig(MatcherConfigType::IncludeOnNoMatchConfig);
  createFilter();

  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::Continue);
}

TEST_F(FilterTest, DecodeHeaderWithInvalidConfig) {
  addMatcherConfig(MatcherConfigType::Invalid);
  createFilter();

  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::Continue);
}

TEST_F(FilterTest, DecodeHeaderWithMismatchHeader) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();
  constructMismatchedRequestHeader();

  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::Continue);
}

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
