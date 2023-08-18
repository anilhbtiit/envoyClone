#include <chrono>
#include <functional>

#include "envoy/http/protocol.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/upstream/host_description.h"

#include "source/common/common/fmt.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stream_info/stream_id_provider_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/stream_info/utility.h"

#include "test/common/stream_info/test_int_accessor.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace StreamInfo {
namespace {

std::chrono::nanoseconds checkDuration(std::chrono::nanoseconds last,
                                       absl::optional<std::chrono::nanoseconds> timing) {
  EXPECT_TRUE(timing);
  EXPECT_LE(last, timing.value());
  return timing.value();
}

class StreamInfoImplTest : public testing::Test {
protected:
  void assertStreamInfoSize(StreamInfoImpl stream_info) {
    ASSERT_TRUE(sizeof(stream_info) == 840 || sizeof(stream_info) == 856 ||
                sizeof(stream_info) == 888 || sizeof(stream_info) == 776 ||
                sizeof(stream_info) == 728 || sizeof(stream_info) == 744)
        << "If adding fields to StreamInfoImpl, please check to see if you "
           "need to add them to setFromForRecreateStream or setFrom! Current size "
        << sizeof(stream_info);
  }
  DangerousDeprecatedTestTime test_time_;
};

TEST_F(StreamInfoImplTest, TimingTest) {
  MonotonicTime pre_start = test_time_.timeSystem().monotonicTime();
  StreamInfoImpl info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);
  info.setUpstreamInfo(std::make_shared<UpstreamInfoImpl>());
  UpstreamTiming& upstream_timing = info.upstreamInfo()->upstreamTiming();
  MonotonicTime post_start = test_time_.timeSystem().monotonicTime();

  const MonotonicTime& start = info.startTimeMonotonic();

  EXPECT_LE(pre_start, start) << "Start time was lower than expected";
  EXPECT_GE(post_start, start) << "Start time was higher than expected";

  TimingUtility timing(info);
  EXPECT_FALSE(timing.lastDownstreamRxByteReceived());
  info.downstreamTiming().onLastDownstreamRxByteReceived(test_time_.timeSystem());
  std::chrono::nanoseconds dur =
      checkDuration(std::chrono::nanoseconds{0}, timing.lastDownstreamRxByteReceived());

  EXPECT_FALSE(timing.firstUpstreamTxByteSent());
  upstream_timing.onFirstUpstreamTxByteSent(test_time_.timeSystem());
  dur = checkDuration(dur, timing.firstUpstreamTxByteSent());

  EXPECT_FALSE(timing.lastUpstreamTxByteSent());
  upstream_timing.onLastUpstreamTxByteSent(test_time_.timeSystem());
  dur = checkDuration(dur, timing.lastUpstreamTxByteSent());

  EXPECT_FALSE(timing.firstUpstreamRxByteReceived());
  upstream_timing.onFirstUpstreamRxByteReceived(test_time_.timeSystem());
  dur = checkDuration(dur, timing.firstUpstreamRxByteReceived());

  EXPECT_FALSE(timing.lastUpstreamRxByteReceived());
  upstream_timing.onLastUpstreamRxByteReceived(test_time_.timeSystem());
  dur = checkDuration(dur, timing.lastUpstreamRxByteReceived());

  EXPECT_FALSE(timing.firstDownstreamTxByteSent());
  info.downstreamTiming().onFirstDownstreamTxByteSent(test_time_.timeSystem());
  dur = checkDuration(dur, timing.firstDownstreamTxByteSent());

  EXPECT_FALSE(timing.lastDownstreamTxByteSent());
  info.downstreamTiming().onLastDownstreamTxByteSent(test_time_.timeSystem());
  dur = checkDuration(dur, timing.lastDownstreamTxByteSent());

  EXPECT_FALSE(timing.downstreamHandshakeComplete());
  info.downstreamTiming().onDownstreamHandshakeComplete(test_time_.timeSystem());
  dur = checkDuration(dur, timing.downstreamHandshakeComplete());

  EXPECT_FALSE(timing.upstreamHandshakeComplete());
  upstream_timing.onUpstreamHandshakeComplete(test_time_.timeSystem());
  dur = checkDuration(dur, timing.upstreamHandshakeComplete());

  EXPECT_FALSE(timing.lastDownstreamAckReceived());
  info.downstreamTiming().onLastDownstreamAckReceived(test_time_.timeSystem());
  dur = checkDuration(dur, timing.lastDownstreamAckReceived());

  EXPECT_FALSE(info.requestComplete());
  info.onRequestComplete();
  dur = checkDuration(dur, info.requestComplete());
}

TEST_F(StreamInfoImplTest, BytesTest) {
  StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);

  const uint64_t bytes_sent = 7;
  const uint64_t bytes_received = 12;

  stream_info.addBytesSent(bytes_sent);
  stream_info.addBytesReceived(bytes_received);

  EXPECT_EQ(bytes_sent, stream_info.bytesSent());
  EXPECT_EQ(bytes_received, stream_info.bytesReceived());
}

TEST_F(StreamInfoImplTest, ResponseFlagTest) {
  const std::vector<ResponseFlag> responseFlags = {FailedLocalHealthCheck,
                                                   NoHealthyUpstream,
                                                   UpstreamRequestTimeout,
                                                   LocalReset,
                                                   UpstreamRemoteReset,
                                                   UpstreamConnectionFailure,
                                                   UpstreamConnectionTermination,
                                                   UpstreamOverflow,
                                                   NoRouteFound,
                                                   DelayInjected,
                                                   FaultInjected,
                                                   RateLimited};

  StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);

  EXPECT_FALSE(stream_info.hasAnyResponseFlag());
  EXPECT_FALSE(stream_info.intersectResponseFlags(0));
  for (ResponseFlag flag : responseFlags) {
    // Test cumulative setting of response flags.
    EXPECT_FALSE(stream_info.hasResponseFlag(flag))
        << fmt::format("Flag: {} was already set", flag);
    stream_info.setResponseFlag(flag);
    EXPECT_TRUE(stream_info.hasResponseFlag(flag))
        << fmt::format("Flag: {} was expected to be set", flag);
  }
  EXPECT_TRUE(stream_info.hasAnyResponseFlag());
  EXPECT_EQ(0xFFF, stream_info.responseFlags());

  StreamInfoImpl stream_info2(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);
  stream_info2.setResponseFlag(FailedLocalHealthCheck);

  EXPECT_TRUE(stream_info2.intersectResponseFlags(FailedLocalHealthCheck));
}

TEST_F(StreamInfoImplTest, MiscSettersAndGetters) {
  {
    StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);

    EXPECT_EQ(nullptr, stream_info.upstreamInfo());
    EXPECT_EQ(Http::Protocol::Http2, stream_info.protocol().value());
    stream_info.setUpstreamInfo(std::make_shared<UpstreamInfoImpl>());

    stream_info.protocol(Http::Protocol::Http10);
    EXPECT_EQ(Http::Protocol::Http10, stream_info.protocol().value());

    EXPECT_FALSE(stream_info.responseCode());
    stream_info.setResponseCode(200);
    ASSERT_TRUE(stream_info.responseCode());
    EXPECT_EQ(200, stream_info.responseCode().value());

    EXPECT_FALSE(stream_info.attemptCount().has_value());
    stream_info.setAttemptCount(93);
    ASSERT_TRUE(stream_info.attemptCount().has_value());
    EXPECT_EQ(stream_info.attemptCount().value(), 93);

    EXPECT_FALSE(stream_info.responseCodeDetails().has_value());
    stream_info.setResponseCodeDetails(ResponseCodeDetails::get().ViaUpstream);
    ASSERT_TRUE(stream_info.responseCodeDetails().has_value());
    EXPECT_EQ(ResponseCodeDetails::get().ViaUpstream, stream_info.responseCodeDetails().value());

    EXPECT_FALSE(stream_info.connectionTerminationDetails().has_value());
    stream_info.setConnectionTerminationDetails("access_denied");
    ASSERT_TRUE(stream_info.connectionTerminationDetails().has_value());
    EXPECT_EQ("access_denied", stream_info.connectionTerminationDetails().value());

    EXPECT_EQ(nullptr, stream_info.upstreamInfo()->upstreamHost());
    Upstream::HostDescriptionConstSharedPtr host(new NiceMock<Upstream::MockHostDescription>());
    stream_info.upstreamInfo()->setUpstreamHost(host);
    EXPECT_EQ(host, stream_info.upstreamInfo()->upstreamHost());

    EXPECT_FALSE(stream_info.healthCheck());
    stream_info.healthCheck(true);
    EXPECT_TRUE(stream_info.healthCheck());

    EXPECT_EQ(nullptr, stream_info.route());
    std::shared_ptr<NiceMock<Router::MockRoute>> route =
        std::make_shared<NiceMock<Router::MockRoute>>();
    stream_info.route_ = route;
    EXPECT_EQ(route, stream_info.route());

    stream_info.filterState()->setData("test", std::make_unique<TestIntAccessor>(1),
                                       FilterState::StateType::ReadOnly,
                                       FilterState::LifeSpan::FilterChain);
    EXPECT_EQ(1, stream_info.filterState()->getDataReadOnly<TestIntAccessor>("test")->access());

    stream_info.upstreamInfo()->setUpstreamFilterState(stream_info.filterState());
    EXPECT_EQ(1, stream_info.upstreamInfo()
                     ->upstreamFilterState()
                     ->getDataReadOnly<TestIntAccessor>("test")
                     ->access());

    EXPECT_EQ(absl::nullopt, stream_info.upstreamClusterInfo());
    Upstream::ClusterInfoConstSharedPtr cluster_info(new NiceMock<Upstream::MockClusterInfo>());
    stream_info.setUpstreamClusterInfo(cluster_info);
    EXPECT_NE(absl::nullopt, stream_info.upstreamClusterInfo());
    EXPECT_EQ("fake_cluster", stream_info.upstreamClusterInfo().value()->name());

    const std::string session_id =
        "D62A523A65695219D46FE1FFE285A4C371425ACE421B110B5B8D11D3EB4D5F0B";
    auto ssl_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*ssl_info, sessionId()).WillRepeatedly(testing::ReturnRef(session_id));
    stream_info.upstreamInfo()->setUpstreamSslConnection(ssl_info);
    EXPECT_EQ(session_id, stream_info.upstreamInfo()->upstreamSslConnection()->sessionId());

    EXPECT_FALSE(stream_info.upstreamInfo()->upstreamConnectionId().has_value());
    stream_info.upstreamInfo()->setUpstreamConnectionId(12345);
    ASSERT_TRUE(stream_info.upstreamInfo()->upstreamConnectionId().has_value());
    EXPECT_EQ(12345, stream_info.upstreamInfo()->upstreamConnectionId().value());

    EXPECT_FALSE(stream_info.upstreamInfo()->upstreamInterfaceName().has_value());
    stream_info.upstreamInfo()->setUpstreamInterfaceName("lo");
    ASSERT_TRUE(stream_info.upstreamInfo()->upstreamInterfaceName().has_value());
    EXPECT_EQ("lo", stream_info.upstreamInfo()->upstreamInterfaceName().value());

    std::shared_ptr<UpstreamInfo> new_info = std::make_shared<UpstreamInfoImpl>();
    EXPECT_NE(stream_info.upstreamInfo(), new_info);
    stream_info.setUpstreamInfo(new_info);
    EXPECT_EQ(stream_info.upstreamInfo(), new_info);
  }
}

TEST_F(StreamInfoImplTest, SetFromForRecreateStream) {
  StreamInfoImpl s1(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);

  s1.addBytesReceived(1);
  s1.downstreamTiming().onLastDownstreamRxByteReceived(test_time_.timeSystem());
  s1.addBytesRetransmitted(1);
  s1.addPacketsRetransmitted(1);

#ifdef __clang__
#if defined(__linux__)
#if defined(__has_feature) && !(__has_feature(thread_sanitizer))
  assertStreamInfoSize(s1);
#endif
#endif
#endif

  StreamInfoImpl s2(Http::Protocol::Http11, test_time_.timeSystem(), nullptr);
  s2.setFromForRecreateStream(s1);
  EXPECT_EQ(s1.startTime(), s2.startTime());
  EXPECT_EQ(s1.startTimeMonotonic(), s2.startTimeMonotonic());
  EXPECT_EQ(s1.downstreamTiming().lastDownstreamRxByteReceived(),
            s2.downstreamTiming().lastDownstreamRxByteReceived());
  EXPECT_EQ(s1.protocol(), s2.protocol());
  EXPECT_EQ(s1.bytesReceived(), s2.bytesReceived());
  EXPECT_EQ(s1.getDownstreamBytesMeter(), s2.getDownstreamBytesMeter());
  EXPECT_EQ(s1.downstreamTransportFailureReason(), s2.downstreamTransportFailureReason());
  EXPECT_EQ(s1.bytesRetransmitted(), s2.bytesRetransmitted());
  EXPECT_EQ(s1.packetsRetransmitted(), s2.packetsRetransmitted());
}

TEST_F(StreamInfoImplTest, SetFrom) {
  StreamInfoImpl s1(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);

  // setFromForRecreateStream
  s1.addBytesReceived(1);
  s1.downstreamTiming().onLastDownstreamRxByteReceived(test_time_.timeSystem());
  s1.addBytesRetransmitted(1);
  s1.addPacketsRetransmitted(1);

  // setFrom
  s1.setRouteName("foo");
  s1.setVirtualClusterName(absl::optional<std::string>("bar"));
  s1.setResponseCode(200);
  s1.setResponseCodeDetails("OK");
  s1.setConnectionTerminationDetails("baz");
  s1.setUpstreamInfo(std::make_shared<UpstreamInfoImpl>());
  s1.upstreamInfo()->upstreamTiming().onLastUpstreamTxByteSent(test_time_.timeSystem());
  s1.onRequestComplete();
  s1.setResponseFlag(FailedLocalHealthCheck);
  s1.healthCheck(true);
  s1.route_ = std::make_shared<NiceMock<Router::MockRoute>>();
  s1.setDynamicMetadata("com.test", MessageUtil::keyValueStruct("test_key", "test_value"));
  s1.filterState()->setData("test", std::make_unique<TestIntAccessor>(1),
                            FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain);
  Http::TestRequestHeaderMapImpl headers1;
  s1.setRequestHeaders(headers1);
  Upstream::ClusterInfoConstSharedPtr cluster_info(new NiceMock<Upstream::MockClusterInfo>());
  s1.setUpstreamClusterInfo(cluster_info);
  s1.setStreamIdProvider(
      std::make_shared<StreamIdProviderImpl>("a121e9e1-feae-4136-9e0e-6fac343d56c9"));
  s1.setTraceReason(Tracing::Reason::ClientForced);
  s1.setFilterChainName("foobar");
  s1.setAttemptCount(5);
  s1.setDownstreamTransportFailureReason("error");
  s1.addBytesSent(1);
  s1.setIsShadow(true);

#ifdef __clang__
#if defined(__linux__)
#if defined(__has_feature) && !(__has_feature(thread_sanitizer))
  assertStreamInfoSize(s1);
#endif
#endif
#endif

  StreamInfoImpl s2(Http::Protocol::Http11, test_time_.timeSystem(), nullptr);
  Http::TestRequestHeaderMapImpl headers2;
  s2.setFrom(s1, &headers2);

  // Copied by setFromForRecreateStream
  EXPECT_EQ(s1.startTime(), s2.startTime());
  EXPECT_EQ(s1.startTimeMonotonic(), s2.startTimeMonotonic());
  EXPECT_EQ(s1.downstreamTiming().lastDownstreamRxByteReceived(),
            s2.downstreamTiming().lastDownstreamRxByteReceived());
  EXPECT_EQ(s1.protocol(), s2.protocol());
  EXPECT_EQ(s1.bytesReceived(), s2.bytesReceived());
  EXPECT_EQ(s1.getDownstreamBytesMeter(), s2.getDownstreamBytesMeter());
  EXPECT_EQ(s1.downstreamTransportFailureReason(), s2.downstreamTransportFailureReason());
  EXPECT_EQ(s1.bytesRetransmitted(), s2.bytesRetransmitted());
  EXPECT_EQ(s1.packetsRetransmitted(), s2.packetsRetransmitted());

  // Copied by setFrom
  EXPECT_EQ(s1.getRouteName(), s2.getRouteName());
  EXPECT_EQ(s1.virtualClusterName(), s2.virtualClusterName());
  EXPECT_EQ(s1.responseCode(), s2.responseCode());
  EXPECT_EQ(s1.responseCodeDetails(), s2.responseCodeDetails());
  EXPECT_EQ(s1.connectionTerminationDetails(), s2.connectionTerminationDetails());
  EXPECT_EQ(s1.upstreamInfo()->upstreamTiming().last_upstream_tx_byte_sent_,
            s2.upstreamInfo()->upstreamTiming().last_upstream_tx_byte_sent_);
  EXPECT_EQ(s1.requestComplete(), s2.requestComplete());
  EXPECT_EQ(s1.responseFlags(), s2.responseFlags());
  EXPECT_EQ(s1.healthCheck(), s2.healthCheck());
  EXPECT_NE(s1.route(), nullptr);
  EXPECT_EQ(s1.route(), s2.route());
  EXPECT_EQ(
      Config::Metadata::metadataValue(&s1.dynamicMetadata(), "com.test", "test_key").string_value(),
      Config::Metadata::metadataValue(&s2.dynamicMetadata(), "com.test", "test_key")
          .string_value());
  EXPECT_EQ(s1.filterState()->getDataReadOnly<TestIntAccessor>("test")->access(),
            s2.filterState()->getDataReadOnly<TestIntAccessor>("test")->access());
  EXPECT_EQ(*s1.getRequestHeaders(), headers1);
  EXPECT_EQ(*s2.getRequestHeaders(), headers2);
  EXPECT_TRUE(s2.upstreamClusterInfo().has_value());
  EXPECT_EQ(s1.upstreamClusterInfo(), s2.upstreamClusterInfo());
  EXPECT_EQ(s1.getStreamIdProvider().value().get().toStringView().value(),
            s2.getStreamIdProvider().value().get().toStringView().value());
  EXPECT_EQ(s1.traceReason(), s2.traceReason());
  EXPECT_EQ(s1.filterChainName(), s2.filterChainName());
  EXPECT_EQ(s1.attemptCount(), s2.attemptCount());
  EXPECT_EQ(s1.getUpstreamBytesMeter(), s2.getUpstreamBytesMeter());
  EXPECT_EQ(s1.bytesSent(), s2.bytesSent());
  EXPECT_EQ(s1.isShadow(), s2.isShadow());
}

TEST_F(StreamInfoImplTest, DynamicMetadataTest) {
  StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);

  EXPECT_EQ(0, stream_info.dynamicMetadata().filter_metadata_size());
  stream_info.setDynamicMetadata("com.test", MessageUtil::keyValueStruct("test_key", "test_value"));
  EXPECT_EQ("test_value",
            Config::Metadata::metadataValue(&stream_info.dynamicMetadata(), "com.test", "test_key")
                .string_value());
  ProtobufWkt::Struct struct_obj2;
  ProtobufWkt::Value val2;
  val2.set_string_value("another_value");
  (*struct_obj2.mutable_fields())["another_key"] = val2;
  stream_info.setDynamicMetadata("com.test", struct_obj2);
  EXPECT_EQ("another_value", Config::Metadata::metadataValue(&stream_info.dynamicMetadata(),
                                                             "com.test", "another_key")
                                 .string_value());
  // make sure "test_key:test_value" still exists
  EXPECT_EQ("test_value",
            Config::Metadata::metadataValue(&stream_info.dynamicMetadata(), "com.test", "test_key")
                .string_value());
  std::string json;
  const auto test_struct = stream_info.dynamicMetadata().filter_metadata().at("com.test");
  const auto status = Protobuf::util::MessageToJsonString(test_struct, &json);
  EXPECT_TRUE(status.ok());
  // check json contains the key and values we set
  EXPECT_TRUE(json.find("\"test_key\":\"test_value\"") != std::string::npos);
  EXPECT_TRUE(json.find("\"another_key\":\"another_value\"") != std::string::npos);
}

TEST_F(StreamInfoImplTest, DumpStateTest) {
  StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);
  std::string prefix = "";

  for (int i = 0; i < 7; ++i) {
    std::stringstream out;
    stream_info.dumpState(out, i);
    std::string state = out.str();
    EXPECT_TRUE(absl::StartsWith(state, prefix));
    EXPECT_THAT(state, testing::HasSubstr("protocol_: 2"));
    prefix = prefix + "  ";
  }
}

TEST_F(StreamInfoImplTest, RequestHeadersTest) {
  StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);
  EXPECT_FALSE(stream_info.getRequestHeaders());

  Http::TestRequestHeaderMapImpl headers;
  stream_info.setRequestHeaders(headers);
  EXPECT_EQ(&headers, stream_info.getRequestHeaders());
}

TEST_F(StreamInfoImplTest, DefaultStreamIdProvider) {
  StreamInfoImpl stream_info(test_time_.timeSystem(), nullptr);
  EXPECT_EQ(false, stream_info.getStreamIdProvider().has_value());
}

TEST_F(StreamInfoImplTest, StreamIdProvider) {
  StreamInfoImpl stream_info(test_time_.timeSystem(), nullptr);
  stream_info.setStreamIdProvider(
      std::make_shared<StreamIdProviderImpl>("a121e9e1-feae-4136-9e0e-6fac343d56c9"));

  EXPECT_EQ(true, stream_info.getStreamIdProvider().has_value());
  EXPECT_EQ("a121e9e1-feae-4136-9e0e-6fac343d56c9",
            stream_info.getStreamIdProvider().value().get().toStringView().value());
  EXPECT_EQ(true, stream_info.getStreamIdProvider().value().get().toInteger().has_value());
}

TEST_F(StreamInfoImplTest, Details) {
  StreamInfoImpl stream_info(test_time_.timeSystem(), nullptr);
  EXPECT_FALSE(stream_info.responseCodeDetails().has_value());
  stream_info.setResponseCodeDetails("two_words");
  ASSERT_TRUE(stream_info.responseCodeDetails().has_value());
  EXPECT_EQ(stream_info.responseCodeDetails().value(), "two_words");
}

TEST_F(StreamInfoImplTest, DownstreamTransportFailureReason) {
  StreamInfoImpl stream_info(test_time_.timeSystem(), nullptr);
  EXPECT_TRUE(stream_info.downstreamTransportFailureReason().empty());
  stream_info.setDownstreamTransportFailureReason("TLS error");
  EXPECT_FALSE(stream_info.downstreamTransportFailureReason().empty());
  EXPECT_EQ(stream_info.downstreamTransportFailureReason(), "TLS error");
}

TEST(UpstreamInfoImplTest, DumpState) {
  UpstreamInfoImpl upstream_info;

  {
    std::stringstream out;
    upstream_info.dumpState(out, 0);
    std::string state = out.str();
    EXPECT_THAT(state, testing::HasSubstr("upstream_connection_id_: null"));
  }
  upstream_info.setUpstreamConnectionId(5);
  {
    std::stringstream out;
    upstream_info.dumpState(out, 0);
    std::string state = out.str();
    EXPECT_THAT(state, testing::HasSubstr("upstream_connection_id_: 5"));
  }
}

} // namespace
} // namespace StreamInfo
} // namespace Envoy
