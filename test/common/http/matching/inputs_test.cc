#include "envoy/http/filter.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/http/matching/inputs.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_impl.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Http {
namespace Matching {

TEST(MatchingData, HttpRequestHeadersDataInput) {
  HttpRequestHeadersDataInput input("header");
  StreamInfo::MockStreamInfo info;
  info.downstream_connection_info_provider_ = std::make_shared<Network::ConnectionInfoSetterImpl>(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));
  HttpMatchingDataImpl data(info);

  {
    TestRequestHeaderMapImpl request_headers({{"header", "bar"}});
    data.onRequestHeaders(request_headers);

    EXPECT_EQ(input.get(data).data_, "bar");
  }

  {
    TestRequestHeaderMapImpl request_headers({{"not-header", "baz"}});
    data.onRequestHeaders(request_headers);
    auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST(MatchingData, HttpRequestTrailersDataInput) {
  HttpRequestTrailersDataInput input("header");
  StreamInfo::MockStreamInfo info;
  info.downstream_connection_info_provider_ = std::make_shared<Network::ConnectionInfoSetterImpl>(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));
  HttpMatchingDataImpl data(info);

  {
    TestRequestTrailerMapImpl request_trailers({{"header", "bar"}});
    data.onRequestTrailers(request_trailers);

    EXPECT_EQ(input.get(data).data_, "bar");
  }

  {
    TestRequestTrailerMapImpl request_trailers({{"not-header", "baz"}});
    data.onRequestTrailers(request_trailers);
    auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST(MatchingData, HttpResponseHeadersDataInput) {
  HttpResponseHeadersDataInput input("header");
  StreamInfo::MockStreamInfo info;
  info.downstream_connection_info_provider_ = std::make_shared<Network::ConnectionInfoSetterImpl>(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));
  HttpMatchingDataImpl data(info);

  {
    TestResponseHeaderMapImpl response_headers({{"header", "bar"}});
    data.onResponseHeaders(response_headers);

    EXPECT_EQ(input.get(data).data_, "bar");
  }

  {
    TestResponseHeaderMapImpl response_headers({{"not-header", "baz"}});
    data.onResponseHeaders(response_headers);
    auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST(MatchingData, HttpResponseTrailersDataInput) {
  HttpResponseTrailersDataInput input("header");
  StreamInfo::MockStreamInfo info;
  info.downstream_connection_info_provider_ = std::make_shared<Network::ConnectionInfoSetterImpl>(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));
  HttpMatchingDataImpl data(info);

  {
    TestResponseTrailerMapImpl response_trailers({{"header", "bar"}});
    data.onResponseTrailers(response_trailers);

    EXPECT_EQ(input.get(data).data_, "bar");
  }

  {
    TestResponseTrailerMapImpl response_trailers({{"not-header", "baz"}});
    data.onResponseTrailers(response_trailers);
    auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

} // namespace Matching
} // namespace Http
} // namespace Envoy
