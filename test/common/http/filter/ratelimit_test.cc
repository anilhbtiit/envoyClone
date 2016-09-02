#include "common/buffer/buffer_impl.h"
#include "common/http/filter/ratelimit.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/runtime/mocks.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::WithArgs;

namespace Http {
namespace RateLimit {

TEST(HttpRateLimitFilterBadConfigTest, All) {
  std::string json = R"EOF(
  {
    "domain": "foo",
    "actions": [
      {"type": "foo"}
    ]
  }
  )EOF";

  Json::StringLoader config(json);
  Stats::IsolatedStoreImpl stats_store;
  NiceMock<Runtime::MockLoader> runtime;
  EXPECT_THROW(FilterConfig(config, "service_cluster", stats_store, runtime), EnvoyException);
}

class HttpRateLimitFilterTest : public testing::Test {
public:
  HttpRateLimitFilterTest() {
    std::string json = R"EOF(
    {
      "domain": "foo",
      "actions": [
        {"type": "service_to_service"}
      ]
    }
    )EOF";

    ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.http_filter_enabled", 100))
        .WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.http_filter_enforcing", 100))
        .WillByDefault(Return(true));

    Json::StringLoader config(json);
    config_.reset(new FilterConfig(config, "service_cluster", stats_store_, runtime_));

    client_ = new ::RateLimit::MockClient();
    filter_.reset(new Filter(config_, ::RateLimit::ClientPtr{client_}));
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
  }

  FilterConfigPtr config_;
  ::RateLimit::MockClient* client_;
  std::unique_ptr<Filter> filter_;
  NiceMock<MockStreamDecoderFilterCallbacks> filter_callbacks_;
  ::RateLimit::RequestCallbacks* request_callbacks_{};
  HeaderMapImpl request_headers_;
  Buffer::OwnedImpl data_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
};

TEST_F(HttpRateLimitFilterTest, NoRoute) {
  EXPECT_CALL(filter_callbacks_.route_table_, routeForRequest(_)).WillOnce(Return(nullptr));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, NoLimiting) {
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, RuntimeDisabled) {
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ratelimit.http_filter_enabled", 100))
      .WillOnce(Return(false));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, OkResponse) {
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_,
              limit(_, "foo",
                    testing::ContainerEq(std::vector<::RateLimit::Descriptor>{
                        {{{"to_cluster", "fake_cluster"}}},
                        {{{"to_cluster", "fake_cluster"}, {"from_cluster", "service_cluster"}}}})))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_headers_));

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(::RateLimit::LimitStatus::OK);

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.ok").value());
}

TEST_F(HttpRateLimitFilterTest, ImmediateOkResponse) {
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_,
              limit(_, "foo",
                    testing::ContainerEq(std::vector<::RateLimit::Descriptor>{
                        {{{"to_cluster", "fake_cluster"}}},
                        {{{"to_cluster", "fake_cluster"}, {"from_cluster", "service_cluster"}}}})))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks) -> void {
        callbacks.complete(::RateLimit::LimitStatus::OK);
      })));

  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.ok").value());
}

TEST_F(HttpRateLimitFilterTest, ErrorResponse) {
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_, limit(_, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(::RateLimit::LimitStatus::Error);

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.error").value());
}

TEST_F(HttpRateLimitFilterTest, LimitResponse) {
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_, limit(_, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  Http::HeaderMapImpl response_headers{{":status", "429"}};
  EXPECT_CALL(filter_callbacks_, encodeHeaders_(HeaderMapEqualRef(response_headers), true));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  request_callbacks_->complete(::RateLimit::LimitStatus::OverLimit);

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.over_limit").value());
  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.upstream_rq_4xx").value());
  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.upstream_rq_429").value());
}

TEST_F(HttpRateLimitFilterTest, LimitResponseRuntimeDisabled) {
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_, limit(_, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ratelimit.http_filter_enforcing", 100))
      .WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(::RateLimit::LimitStatus::OverLimit);

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.over_limit").value());
  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.upstream_rq_4xx").value());
  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.upstream_rq_429").value());
}

TEST_F(HttpRateLimitFilterTest, ResetDuringCall) {
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_, limit(_, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(*client_, cancel());
  filter_callbacks_.reset_callback_();
}

} // RateLimit
} // Http
