#include "envoy/config/filter/udp/dns_filter/v2alpha/dns_filter.pb.h"
#include "envoy/config/filter/udp/dns_filter/v2alpha/dns_filter.pb.validate.h"

#include "common/common/logger.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"

#include "dns_filter_test_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AtLeast;
using testing::InSequence;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace {

Api::IoCallUint64Result makeNoError(uint64_t rc) {
  auto no_error = Api::ioCallUint64ResultNoError();
  no_error.rc_ = rc;
  return no_error;
}

class DnsFilterTest : public testing::Test {
public:
  DnsFilterTest()
      : listener_address_(Network::Utility::parseInternetAddressAndPort("127.0.2.1:5353")) {
    // TODO: Consume the log setting from the command line
    // Logger::Registry::setLogLevel(TestEnvironment::getOptions().logLevel());
    Logger::Registry::setLogLevel(spdlog::level::trace);

    EXPECT_CALL(callbacks_, udpListener()).Times(AtLeast(0));
    EXPECT_CALL(callbacks_.udp_listener_, send(_))
        .WillRepeatedly(
            Invoke([this](const Network::UdpSendData& send_data) -> Api::IoCallUint64Result {
              response_ptr = std::make_unique<Buffer::OwnedImpl>();
              response_ptr->move(send_data.buffer_);
              return makeNoError(response_ptr->length());
            }));

    EXPECT_CALL(callbacks_.udp_listener_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
  }

  ~DnsFilterTest() override { EXPECT_CALL(callbacks_.udp_listener_, onDestroy()); }

  void setup(const std::string& yaml) {
    envoy::config::filter::udp::dns_filter::v2alpha::DnsFilterConfig config;
    TestUtility::loadFromYamlAndValidate(yaml, config);
    auto store = stats_store_.createScope("dns_scope");
    EXPECT_CALL(listener_factory_, scope()).WillOnce(ReturnRef(*store));
    EXPECT_CALL(listener_factory_, dispatcher()).Times(AtLeast(0));
    EXPECT_CALL(listener_factory_, clusterManager()).Times(AtLeast(0));

    config_ = std::make_shared<DnsFilterEnvoyConfig>(listener_factory_, config);
    filter_ = std::make_unique<DnsFilter>(callbacks_, config_);
  }

  void sendQueryFromClient(const std::string& peer_address, const std::string& buffer) {
    Network::UdpRecvData data;
    data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPort(peer_address);
    data.addresses_.local_ = listener_address_;
    data.buffer_ = std::make_unique<Buffer::OwnedImpl>(buffer);
    data.receive_time_ = MonotonicTime(std::chrono::seconds(0));
    filter_->onData(data);
  }

  const Network::Address::InstanceConstSharedPtr listener_address_;
  Server::Configuration::MockListenerFactoryContext listener_factory_;
  DnsFilterEnvoyConfigSharedPtr config_;

  std::unique_ptr<DnsFilter> filter_;
  Network::MockUdpReadFilterCallbacks callbacks_;
  Stats::IsolatedStoreImpl stats_store_;
  Buffer::InstancePtr response_ptr;
  DnsMessageParser response_parser_;

  Event::MockDispatcher dispatcher_;

  // This config has external resolution disabled and is used to verify local lookups. With
  // external resolution disabled, it eliminates having to setup mocks for the resolver callbacks in
  // each test.
  const std::string forward_query_on_config = R"EOF(
stat_prefix: "my_prefix"
client_config:
  forward_query: true
  upstream_resolvers:
  - "1.1.1.1"
  - "8.8.8.8"
  - "8.8.4.4"
server_config:
  inline_dns_table:
    external_retry_count: 3
    known_suffixes:
    - suffix: foo1.com
    - suffix: foo2.com
    virtual_domains:
    - name: "www.foo1.com"
      endpoint:
        address_list:
          address:
          - 10.0.0.1
          - 10.0.0.2
    - name: "www.foo2.com"
      endpoint:
        address_list:
          address:
          - 2001:8a:c1::2800:7
          - 2001:8a:c1::2800:8
          - 2001:8a:c1::2800:9
    - name: "www.foo3.com"
      endpoint:
        address_list:
          address:
          - 10.0.3.1
  )EOF";
};

TEST_F(DnsFilterTest, InvalidQuery) {
  InSequence s;

  setup(forward_query_on_config);

  sendQueryFromClient("10.0.0.1:1000", "hello");

  ASSERT_FALSE(response_parser_.parseDnsObject(response_ptr));
}

TEST_F(DnsFilterTest, SingleTypeAQuery) {
  InSequence s;

  setup(forward_query_on_config);

  const std::string domain("www.foo3.com");
  const std::string query =
      Utils::buildQueryForDomain(domain, DnsRecordType::A, DnsRecordClass::IN);
  ASSERT_FALSE(query.empty());

  sendQueryFromClient("10.0.0.1:1000", query);

  // This will fail since the response generation is not being done yet
  ASSERT_FALSE(response_parser_.parseDnsObject(response_ptr));
}

} // namespace
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
