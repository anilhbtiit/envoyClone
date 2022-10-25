#include <chrono>
#include <cstddef>
#include <memory>

#include "envoy/tcp/conn_pool.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_time.h"

#include "contrib/sip_proxy/filters/network/source/app_exception_impl.h"
#include "contrib/sip_proxy/filters/network/source/config.h"
#include "contrib/sip_proxy/filters/network/source/router/config.h"
#include "contrib/sip_proxy/filters/network/source/router/router_impl.h"
#include "contrib/sip_proxy/filters/network/source/sip.h"
#include "contrib/sip_proxy/filters/network/test/mocks.h"
#include "contrib/sip_proxy/filters/network/test/utility.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace Router {

class SipRouterTest : public testing::Test {
public:
  SipRouterTest() : thread_factory_(Thread::threadFactoryForTest()), 
  origin_ingress_(OriginIngress("", "")), stats_(SipFilterStats::generateStats("test.", store_)) {}
  ~SipRouterTest() override { delete (filter_); }

  void initializeTrans(const std::string& sip_protocol_options_yaml = "",
                       const std::string& sip_proxy_yaml = "") {

    if (sip_proxy_yaml.empty()) {
      std::string sip_proxy_yaml1 = R"EOF(
           stat_prefix: egress_sip
           route_config:
             routes:
             - match:
                domain: "icscf-internal.cncs.svc.cluster.local"
                header: "Route"
                parameter: "x-suri"
               route:
                cluster: fake_cluster
             - match:
                domain: "scscf-internal.cncs.svc.cluster.local"
                header: "Route"
                parameter: "x-suri"
               route:
                cluster: fake_cluster2
           settings:
             transaction_timeout: 32s
             local_services:
             - domain: "pcsf-cfed.cncs.svc.cluster.local"
               parameter: "x-suri"
             tra_service_config:
               grpc_service:
                 envoy_grpc:
                   cluster_name: tra_service
               timeout: 2s
               transport_api_version: V3
)EOF";
      TestUtility::loadFromYaml(sip_proxy_yaml1, sip_proxy_config_);
    } else {
      TestUtility::loadFromYaml(sip_proxy_yaml, sip_proxy_config_);
    }

    if (sip_protocol_options_yaml.empty()) {
      const std::string sip_protocol_options_yaml1 = R"EOF(
        session_affinity: true
        registration_affinity: true
        customized_affinity:
          entries:
          - key_name: lskpmc
            query: true
            subscribe: true
          - key_name: ep
            query: false
            subscribe: false
)EOF";
      TestUtility::loadFromYaml(sip_protocol_options_yaml1, sip_protocol_options_config_);
    } else {
      TestUtility::loadFromYaml(sip_protocol_options_yaml, sip_protocol_options_config_);
    }

    const auto options = std::make_shared<ProtocolOptionsConfigImpl>(sip_protocol_options_config_);
    EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
                extensionProtocolOptions(_))
        .WillRepeatedly(Return(options));

    EXPECT_CALL(api_, threadFactory()).WillRepeatedly(testing::ReturnRef(thread_factory_));
    EXPECT_CALL(context_, api()).WillRepeatedly(testing::ReturnRef(api_));
    EXPECT_CALL(context_, getTransportSocketFactoryContext())
        .WillRepeatedly(testing::ReturnRef(factory_context_));
    EXPECT_CALL(factory_context_, localInfo()).WillRepeatedly(testing::ReturnRef(local_info_));

    transaction_infos_ = std::make_shared<TransactionInfos>();
    context_.cluster_manager_.initializeThreadLocalClusters({cluster_name_});

    downstream_connection_infos_ = std::make_shared<DownstreamConnectionInfos>(thread_local_);
    downstream_connection_infos_->init();

    upstream_transaction_infos_ = std::make_shared<UpstreamTransactionInfos>(
        thread_local_, static_cast<std::chrono::seconds>(2));
    upstream_transaction_infos_->init();

    sip_settings_ = std::make_shared<SipSettings>(sip_proxy_config_.settings());

    StreamInfo::StreamInfoImpl stream_info{time_source_, nullptr};
    EXPECT_CALL(config_, stats()).WillRepeatedly(ReturnRef(stats_));
    EXPECT_CALL(config_, settings()).WillRepeatedly(Return(sip_settings_));

    filter_ = new NiceMock<MockConnectionManager>(config_, random_, time_source_, context_, transaction_infos_, downstream_connection_infos_, upstream_transaction_infos_);
    EXPECT_CALL(*filter_, settings()).WillRepeatedly(Return(sip_settings_));

    tra_handler_ = std::make_shared<NiceMock<SipProxy::MockTrafficRoutingAssistantHandler>>(
        *filter_, dispatcher_, sip_proxy_config_.settings().tra_service_config(), context_,
        stream_info);
    
    filter_callbacks_ = std::make_shared<NiceMock<SipFilters::MockDecoderFilterCallbacks>>();
    downstream_connection_infos_->insertDownstreamConnection("xyz", filter_callbacks_);

    origin_ingress_ = OriginIngress(Utility::threadId(context_), "xyz");
  }

  void initializeRouter() {
    route_ = new NiceMock<MockRoute>();
    route_ptr_.reset(route_);

    router_ =
        std::make_unique<Router>(context_.clusterManager(), "test", context_.scope(), context_);

    EXPECT_EQ(nullptr, router_->downstreamConnection());

    EXPECT_CALL(callbacks_, settings()).WillRepeatedly(Return(sip_settings_));
    ON_CALL(callbacks_, transactionInfos()).WillByDefault(Return(transaction_infos_));
    ON_CALL(callbacks_, downstreamConnectionInfos()).WillByDefault(Return(downstream_connection_infos_));
    ON_CALL(callbacks_, upstreamTransactionInfos()).WillByDefault(Return(upstream_transaction_infos_));
    EXPECT_CALL(callbacks_, traHandler()).WillRepeatedly(Return(tra_handler_));
    callbacks_.origin_ingress_ = origin_ingress_;
    ON_CALL(callbacks_, originIngress()).WillByDefault(Return(origin_ingress_));
    router_->setDecoderFilterCallbacks(callbacks_);
  }

  void initializeRouterWithCallback() {
    route_ = new NiceMock<MockRoute>();
    route_ptr_.reset(route_);

    router_ =
        std::make_unique<Router>(context_.clusterManager(), "test", context_.scope(), context_);

    EXPECT_CALL(callbacks_, settings()).WillRepeatedly(Return(sip_settings_));
    EXPECT_CALL(callbacks_, transactionInfos()).WillOnce(Return(transaction_infos_));
    router_->setDecoderFilterCallbacks(callbacks_);

    EXPECT_EQ(nullptr, router_->downstreamConnection());
  }

  void initializeMetadata(MsgType msg_type, MethodType method = MethodType::Invite,
                          bool set_destination = true, std::string additional_headers = "") {

    const std::string SIP_INVITE = // addNewMsgHeader needs a raw_msg with a Content header
        "INVITE sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
        "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
        "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
        "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
        "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
        "Content-Type: application/sdp\x0d\x0a" 
        + additional_headers +
        "Content-Length:  0\x0d\x0a"
        "\x0d\x0a";
    Buffer::OwnedImpl buffer_;
    buffer_.add(SIP_INVITE);

    metadata_ = std::make_shared<MessageMetadata>(buffer_.toString());
    metadata_->setMethodType(method);
    metadata_->setMsgType(msg_type);
    metadata_->setTransactionId("<branch=cluster>");
    metadata_->setEP("10.0.0.1");
    metadata_->affinity().emplace_back("Route", "ep", "ep", false, false);
    metadata_->addMsgHeader(
        HeaderType::Route,
        "Route: "
        "<sip:test@pcsf-cfed.cncs.svc.cluster.local;role=anch;lr;transport=udp;x-suri="
        "sip:scscf-internal.cncs.svc.cluster.local:5060;ep=10.0.0.1>");
    metadata_->addMsgHeader(HeaderType::From, "User.0001@10.0.0.1:5060");
    metadata_->resetAffinityIteration();
    if (set_destination) {
      metadata_->setDestination("10.0.0.1");
    }
  }

  void initializeMetadataResponse(MethodType method = MethodType::Ok200,
                          bool set_destination = true, std::string additional_headers = "") {
    const std::string SIP_OK200_FULL =
        "SIP/2.0 200 OK\x0d\x0a"
        "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
        "CSeq: 1 INVITE\x0d\x0a"
        "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
        "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
        "Record-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
        "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
        "Service-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
        "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=cluster\x0d\x0a"
        "Path: "
        "<sip:10.177.8.232;x-fbi=cfed;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;inst-ip=192."
        "169.110.53;lr;ottag=ue_term;bidx=563242011197570;access-type=ADSL;x-alu-prset-id>\x0d\x0a"
        "P-Nokia-Cookie-IP-Mapping: S1F1=10.0.0.1\x0d\x0a"
        + additional_headers +
        "Content-Length:  0\x0d\x0a"
        "\x0d\x0a";
    Buffer::OwnedImpl buffer_;
    buffer_.add(SIP_OK200_FULL);

    metadata_ = std::make_shared<MessageMetadata>(buffer_.toString());
    metadata_->setMethodType(method);
    metadata_->setMsgType(MsgType::Response);
    metadata_->setTransactionId("<branch=cluster>");
    metadata_->setEP("10.0.0.1");
    metadata_->affinity().emplace_back("Route", "ep", "ep", false, false);
    metadata_->addMsgHeader(
        HeaderType::Route,
        "Route: "
        "<sip:test@pcsf-cfed.cncs.svc.cluster.local;role=anch;lr;transport=udp;x-suri="
        "sip:scscf-internal.cncs.svc.cluster.local:5060;ep=10.0.0.1>");
    metadata_->addMsgHeader(HeaderType::From, "User.0001@10.0.0.1:5060");
    metadata_->resetAffinityIteration();
    if (set_destination) {
      metadata_->setDestination("10.0.0.1");
    }
  }

  void initializeTransaction() {
    auto transaction_info_ptr = std::make_shared<TransactionInfo>(
        cluster_name_, thread_local_, static_cast<std::chrono::seconds>(2));
    transaction_info_ptr->init();
    transaction_infos_->emplace(cluster_name_, transaction_info_ptr);
  }

  void startRequest(FilterStatus status = FilterStatus::StopIteration) {
    EXPECT_CALL(callbacks_, route()).WillRepeatedly(Return(route_ptr_));
    EXPECT_CALL(*route_, routeEntry()).WillRepeatedly(Return(&route_entry_));
    EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
    EXPECT_EQ(FilterStatus::Continue, router_->transportBegin(metadata_));

    EXPECT_EQ(status, router_->messageBegin(metadata_));
  }

  void startResponse(FilterStatus status = FilterStatus::StopIteration) {
    startRequest(status);
  }

  void connectUpstream() {
    EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
                addUpstreamCallbacks(_))
        .WillOnce(Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void {
          upstream_callbacks_ = &cb;
        }));

    conn_state_.reset();
    EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
                connectionState())
        .WillRepeatedly(
            Invoke([&]() -> Tcp::ConnectionPool::ConnectionState* { return conn_state_.get(); }));
    context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(upstream_connection_);

    EXPECT_NE(nullptr, upstream_callbacks_);
  }

  void startRequestWithExistingConnection(MsgType msg_type,
                                          MethodType method = MethodType::Invite) {
    initializeMetadata(msg_type, method);
    EXPECT_EQ(FilterStatus::Continue, router_->transportBegin({}));

    EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

    EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
    EXPECT_NE(nullptr, upstream_callbacks_);
  }

  void completeRequest() {
    EXPECT_EQ(FilterStatus::Continue, router_->messageEnd());
    EXPECT_EQ(FilterStatus::Continue, router_->transportEnd());
  }

  void completeResponse() {
    completeRequest();
  }

  void returnResponse(MsgType msg_type = MsgType::Response) {
    Buffer::OwnedImpl buffer;

    const std::string SIP_OK200_FULL =
        "SIP/2.0 200 OK\x0d\x0a"
        "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
        "CSeq: 1 INVITE\x0d\x0a"
        "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
        "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
        "Record-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
        "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
        "Service-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
        "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=cluster\x0d\x0a"
        "Path: "
        "<sip:10.177.8.232;x-fbi=cfed;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;inst-ip=192."
        "169.110.53;lr;ottag=ue_term;bidx=563242011197570;access-type=ADSL;x-alu-prset-id>\x0d\x0a"
        "P-Nokia-Cookie-IP-Mapping: S1F1=10.0.0.1\x0d\x0a"
        "Content-Length:  0\x0d\x0a"
        "\x0d\x0a";
    buffer.add(SIP_OK200_FULL);

    initializeMetadata(msg_type, MethodType::Ok200, false);

    EXPECT_CALL(*tra_handler_, retrieveTrafficRoutingAssistant(_, _, _, _, _))
        .WillRepeatedly(
            Invoke([&](const std::string&, const std::string&, const absl::optional<TraContextMap>,
                       SipFilters::DecoderFilterCallbacks&, std::string& host) -> QueryStatus {
              host = "10.0.0.11";
              return QueryStatus::Pending;
            }));
    upstream_callbacks_->onUpstreamData(buffer, false);
  }

  void returnResponseNoActiveTrans(MsgType msg_type = MsgType::Response) {
    Buffer::OwnedImpl buffer;

    const std::string SIP_OK200_FULL =
        "SIP/2.0 200 OK\x0d\x0a"
        "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
        "CSeq: 1 INVITE\x0d\x0a"
        "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
        "Record-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
        "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
        "Service-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
        "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=111\x0d\x0a"
        "Path: "
        "<sip:10.177.8.232;x-fbi=cfed;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;inst-ip=192."
        "169.110.53;lr;ottag=ue_term;bidx=563242011197570;access-type=ADSL;x-alu-prset-id>\x0d\x0a"
        "Content-Length:  0\x0d\x0a"
        "\x0d\x0a";
    buffer.add(SIP_OK200_FULL);

    initializeMetadata(msg_type, MethodType::Ok200, false);

    metadata_->setTransactionId("");
    upstream_callbacks_->onUpstreamData(buffer, false);
  }

  void returnResponseNoTransId(MsgType msg_type = MsgType::Response) {
    Buffer::OwnedImpl buffer;

    const std::string SIP_OK200_FULL =
        "SIP/2.0 200 OK\x0d\x0a"
        "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
        "CSeq: 1 INVITE\x0d\x0a"
        "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
        "Record-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
        "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
        "Service-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
        "Via: SIP/2.0/TCP 11.0.0.10:15060;\x0d\x0a"
        "Path: "
        "<sip:10.177.8.232;x-fbi=cfed;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;inst-ip=192."
        "169.110.53;lr;ottag=ue_term;bidx=563242011197570;access-type=ADSL;x-alu-prset-id>\x0d\x0a"
        "Content-Length:  0\x0d\x0a"
        "\x0d\x0a";
    buffer.add(SIP_OK200_FULL);

    initializeMetadata(msg_type, MethodType::Ok200, false);

    metadata_->setTransactionId("");
    upstream_callbacks_->onUpstreamData(buffer, false);
  }

  void initUpstreamRequest(const std::string& origin_ingress_value, std::string additional_headers = "") {
    Buffer::OwnedImpl buffer;
    if (!origin_ingress_value.empty()) {
      initializeMetadata(MsgType::Request, MethodType::Invite, false, "X-Envoy-Origin-Ingress:" + origin_ingress_value + "\x0d\x0a" + additional_headers);
    } else {
      initializeMetadata(MsgType::Request, MethodType::Invite, false,  additional_headers);
    }
    buffer.add(metadata_->rawMsg());
    upstream_callbacks_->onUpstreamData(buffer, false);
  }

  void initUpstreamRequest(Buffer::Instance& buffer) {
    initializeMetadata(MsgType::Request, MethodType::Invite, false);
    buffer.add(metadata_->rawMsg());
    upstream_callbacks_->onUpstreamData(buffer, false);
  }

  void destroyRouter() {
    router_->onDestroy();
    router_.reset();
  }

  void destroyRouterOutofRange() {
    // std::out_of_range Exception
    EXPECT_CALL(callbacks_, transactionId())
        .Times(2)
        .WillOnce(Return("test"))
        .WillOnce(Return("test1"));

    router_->onDestroy();
    router_.reset();
  }

  envoy::extensions::filters::network::sip_proxy::v3alpha::SipProtocolOptions
      sip_protocol_options_config_;
  envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy sip_proxy_config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<Network::MockClientConnection> connection_;
  NiceMock<StreamInfo::MockStreamInfo> streamInfo_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<MockTimeSystem> time_source_;
  NiceMock<SipFilters::MockDecoderFilterCallbacks> callbacks_;
  NiceMock<MockRoute>* route_{};
  NiceMock<MockRouteEntry> route_entry_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Upstream::MockHostDescription>* host_{};
  Tcp::ConnectionPool::ConnectionStatePtr conn_state_;
  Buffer::OwnedImpl buffer_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  NiceMock<MockConnectionManager>* filter_{};
  std::shared_ptr<SipFilters::MockDecoderFilterCallbacks> filter_callbacks_;
  NiceMock<MockConfig> config_;
  NiceMock<Random::MockRandomGenerator> random_;
  Stats::TestUtil::TestStore store_;
  NiceMock<Api::MockApi> api_;
  Thread::ThreadFactory& thread_factory_;

  std::shared_ptr<TransactionInfos> transaction_infos_;
  std::shared_ptr<SipProxy::DownstreamConnectionInfos> downstream_connection_infos_;
  std::shared_ptr<SipProxy::UpstreamTransactionInfos> upstream_transaction_infos_;
  std::shared_ptr<SipSettings> sip_settings_;
  OriginIngress origin_ingress_;
  SipFilterStats stats_;
  

  RouteConstSharedPtr route_ptr_;
  std::unique_ptr<Router> router_;

  std::shared_ptr<SipProxy::MockTrafficRoutingAssistantHandler> tra_handler_;

  std::string cluster_name_{"fake_cluster"};

  MsgType msg_type_{MsgType::Request};
  MessageMetadataSharedPtr metadata_;

  Tcp::ConnectionPool::UpstreamCallbacks* upstream_callbacks_{};
  NiceMock<Network::MockClientConnection> upstream_connection_;
};

TEST_F(SipRouterTest, CustomizedAffinity) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();

  initializeMetadata(MsgType::Request);
  metadata_->setPCookieIpMap({"S1F1", "10.0.0.1"});

  startRequest();
  connectUpstream();
  completeRequest();
  returnResponseNoTransId();
  EXPECT_CALL(callbacks_, transactionId()).WillRepeatedly(Return("test"));
  destroyRouter();
}

TEST_F(SipRouterTest, SessionAffinity) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();
  completeRequest();
  returnResponse();
  EXPECT_CALL(callbacks_, transactionId()).WillRepeatedly(Return("test"));
  destroyRouter();
}

TEST_F(SipRouterTest, SendAnotherMsgInConnectedUpstreamConnection) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();
  completeRequest();
  returnResponseNoActiveTrans();

  EXPECT_EQ(FilterStatus::Continue, router_->transportBegin(metadata_));
  EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
}

TEST_F(SipRouterTest, NoTcpConnPool) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_, tcpConnPool(_, _))
      .WillOnce(Return(absl::nullopt));
  try {
    startRequest(FilterStatus::Continue);
  } catch (const AppException& ex) {
    EXPECT_EQ(1U, context_.scope().counterFromString("test.no_healthy_upstream").value());
  }
}

TEST_F(SipRouterTest, NoTcpConnPoolEmptyDest) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  metadata_->addMsgHeader(HeaderType::Route,
                          "Route: "
                          "<sip:test@pcsf-cfed.cncs.svc.cluster.local;role=anch;lr;transport=udp;"
                          "x-suri=sip:scscf-internal.cncs.svc.cluster.local:5060>");
  metadata_->affinity().emplace_back("Route", "ep", "ep", false, false);
  metadata_->resetAffinityIteration();

  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_, tcpConnPool(_, _))
      .WillOnce(Return(absl::nullopt));
  try {
    startRequest(FilterStatus::Continue);
  } catch (const AppException& ex) {
    EXPECT_EQ(1U, context_.scope().counterFromString("test.no_healthy_upstream").value());
  }
}

TEST_F(SipRouterTest, QueryPending) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  metadata_->addMsgHeader(HeaderType::Route,
                          "Route: "
                          "<sip:test@pcsf-cfed.cncs.svc.cluster.local;role=anch;lr;transport=udp;"
                          "x-suri=sip:scscf-internal.cncs.svc.cluster.local:5060>");
  metadata_->affinity().emplace_back("Route", "lskpmc", "S1F1", false, false);
  metadata_->resetAffinityIteration();
  EXPECT_CALL(*tra_handler_, retrieveTrafficRoutingAssistant(_, _, _, _, _))
      .WillRepeatedly(
          Invoke([&](const std::string&, const std::string&, const absl::optional<TraContextMap>,
                     SipFilters::DecoderFilterCallbacks&, std::string& host) -> QueryStatus {
            host = "10.0.0.11";
            return QueryStatus::Pending;
          }));
  startRequest();
}

TEST_F(SipRouterTest, QueryStop) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  metadata_->affinity().clear();
  metadata_->affinity().emplace_back("Route", "lskpmc", "S1F1", false, false);
  metadata_->resetAffinityIteration();
  EXPECT_CALL(*tra_handler_, retrieveTrafficRoutingAssistant(_, _, _, _, _))
      .WillRepeatedly(
          Invoke([&](const std::string&, const std::string&, const absl::optional<TraContextMap>,
                     SipFilters::DecoderFilterCallbacks&, std::string& host) -> QueryStatus {
            host = "10.0.0.11";
            return QueryStatus::Stop;
          }));
  startRequest(FilterStatus::Continue);
}

TEST_F(SipRouterTest, SendAnotherMsgInConnectingUpstreamConnection) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();

  EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));
}

TEST_F(SipRouterTest, CallNoRoute) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  metadata_->affinity().clear();

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(nullptr));
  try {
    EXPECT_EQ(FilterStatus::StopIteration, router_->transportBegin(metadata_));
  } catch (const AppException& ex) {
    EXPECT_EQ(1U, context_.scope().counterFromString("test.route_missing").value());
  }

  destroyRouterOutofRange();
}

TEST_F(SipRouterTest, CallNoCluster) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  metadata_->affinity().clear();

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(Eq(cluster_name_)))
      .WillOnce(Return(nullptr));

  try {
    EXPECT_EQ(FilterStatus::StopIteration, router_->transportBegin(metadata_));
  } catch (const AppException& ex) {
    EXPECT_EQ(1U, context_.scope().counterFromString("test.unknown_cluster").value());
  }

  destroyRouter();
}

TEST_F(SipRouterTest, ClusterMaintenanceMode) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.cluster_.info_, maintenanceMode())
      .WillOnce(Return(true));

  try {
    EXPECT_EQ(FilterStatus::StopIteration, router_->transportBegin(metadata_));
  } catch (const AppException& ex) {
    EXPECT_EQ(1U, context_.scope().counterFromString("test.upstream_rq_maintenance_mode").value());
  }
  destroyRouter();
}

TEST_F(SipRouterTest, NoHost) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillOnce(ReturnRef(cluster_name_));
  EXPECT_EQ(FilterStatus::Continue, router_->transportBegin(metadata_));

  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_, host())
      .WillOnce(Return(nullptr));
  EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
  destroyRouter();
}

TEST_F(SipRouterTest, DestNotEqualToHost) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillOnce(ReturnRef(cluster_name_));
  EXPECT_EQ(FilterStatus::Continue, router_->transportBegin(metadata_));

  metadata_->listHeader(HeaderType::Route).clear();
  metadata_->addMsgHeader(
      HeaderType::Route,
      "Route: "
      "<sip:test@pcsf-cfed.cncs.svc.cluster.local;role=anch;lr;transport=udp;x-suri="
      "sip:scscf-internal.cncs.svc.cluster.local:5060;ep=192.168.0.1>");

  metadata_->resetAffinityIteration();

  EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
  destroyRouter();
}

TEST_F(SipRouterTest, CallWithExistingConnection) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();
  completeRequest();
  returnResponse();

  auto& transaction_info_ptr = (*transaction_infos_)[cluster_name_];
  transaction_info_ptr->getUpstreamConnection("10.0.0.1")
      ->setConnectionState(ConnectionState::NotConnected);

  metadata_->affinity().emplace_back("Route", "ep", "ep", false, false);
  metadata_->resetAffinityIteration();

  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_, newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.newConnectionImpl(cb);
            context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(
                upstream_connection_);
            return nullptr;
          }));
  EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
  destroyRouter();
}

TEST_F(SipRouterTest, CallWithExistingConnectionDefaultLoadBalance) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();
  completeRequest();
  returnResponse();

  auto& transaction_info_ptr = (*transaction_infos_)[cluster_name_];
  transaction_info_ptr->getUpstreamConnection("10.0.0.1")
      ->setConnectionState(ConnectionState::NotConnected);

  // initializeMetadata(MsgType::Request);
  metadata_->resetDestination();

  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_, newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.newConnectionImpl(cb);
            context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(
                upstream_connection_);
            return nullptr;
          }));
  EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
  EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
  destroyRouter();
}

TEST_F(SipRouterTest, PoolFailure) {
  initializeTrans();
  initializeRouterWithCallback();
  initializeTransaction();
  initializeMetadata(MsgType::Response);
  startRequest();
  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
}

TEST_F(SipRouterTest, NextAffinityAfterPoolFailure) {
  initializeTrans();
  initializeRouterWithCallback();
  initializeTransaction();
  initializeMetadata(MsgType::Response);
  startRequest();
  metadata_->affinity().emplace_back("Route", "ep", "ep", false, false);
  metadata_->resetAffinityIteration();
  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
}

TEST_F(SipRouterTest, NewConnectionFailure) {
  initializeTrans();
  initializeRouterWithCallback();
  initializeTransaction();
  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_, newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.newConnectionImpl(cb);
            context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(
                upstream_connection_);
            return nullptr;
          }));
  initializeMetadata(MsgType::Response);
  startRequest(FilterStatus::Continue);
}

TEST_F(SipRouterTest, UpstreamCloseMidResponse) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();

  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
  upstream_callbacks_->onEvent(Network::ConnectionEvent::RemoteClose);
  upstream_callbacks_->onEvent(static_cast<Network::ConnectionEvent>(9999));
}

TEST_F(SipRouterTest, RouteEntryImplBase) {
  const envoy::extensions::filters::network::sip_proxy::v3alpha::Route route;
  GeneralRouteEntryImpl* base = new GeneralRouteEntryImpl(route);
  EXPECT_EQ("", base->clusterName());
  EXPECT_EQ(base, base->routeEntry());
  EXPECT_EQ(nullptr, base->metadataMatchCriteria());
}

TEST_F(SipRouterTest, RouteMatch) {
  const std::string yaml = R"EOF(
             routes:
             - match:
                domain: "icscf-internal.cncs.svc.cluster.local"
                header: "Route"
                parameter: "x-suri"
               route:
                cluster: fake_cluster
             - match:
                domain: "scscf-internal.cncs.svc.cluster.local"
                header: "Route"
                parameter: "x-suri"
               route:
                cluster: fake_cluster2
)EOF";

  envoy::extensions::filters::network::sip_proxy::v3alpha::RouteConfiguration config;
  TestUtility::loadFromYaml(yaml, config);

  initializeMetadata(MsgType::Request);
  auto matcher_ptr = std::make_shared<RouteMatcher>(config);

  // Match domain
  metadata_->addMsgHeader(
      HeaderType::Route,
      "Route: "
      "<sip:test@pcsf-cfed.cncs.svc.cluster.local;role=anch;lr;transport=udp;x-suri="
      "sip:scscf-internal.cncs.svc.cluster.local:5060>");

  EXPECT_NE(nullptr, matcher_ptr->route(*metadata_));
}

TEST_F(SipRouterTest, RouteEmptyDomain) {

  const std::string yaml = R"EOF(
             routes:
             - match:
                domain: ""
                header: "Route"
                parameter: "x-suri"
               route:
                cluster: fake_cluster
)EOF";

  envoy::extensions::filters::network::sip_proxy::v3alpha::RouteConfiguration config;
  TestUtility::loadFromYaml(yaml, config);

  initializeMetadata(MsgType::Request);
  auto matcher_ptr = std::make_shared<RouteMatcher>(config);

  // Match domain
  metadata_->addMsgHeader(
      HeaderType::Route,
      "Route: "
      "<sip:test@pcsf-cfed.cncs.svc.cluster.local;role=anch;lr;transport=udp;x-suri="
      "sip:scscf-internal.cncs.svc.cluster.local:5060>");

  EXPECT_EQ(nullptr, matcher_ptr->route(*metadata_));
}

TEST_F(SipRouterTest, RouteDefaultDomain) {

  const std::string yaml = R"EOF(
             routes:
             - match:
                domain: "pcsf-cfed.cncs.svc.cluster.local"
                header: "Route"
                parameter: "x-suri"
               route:
                cluster: fake_cluster
)EOF";

  envoy::extensions::filters::network::sip_proxy::v3alpha::RouteConfiguration config;
  TestUtility::loadFromYaml(yaml, config);

  initializeMetadata(MsgType::Request);
  auto matcher_ptr = std::make_shared<RouteMatcher>(config);

  // Match domain
  metadata_->addMsgHeader(
      HeaderType::Route,
      "Route: "
      "<sip:test@pcsf-cfed.cncs.svc.cluster.local;role=anch;lr;transport=udp;x-suri="
      "sip:scscf-internal.cncs.svc.cluster.local:5060>");

  EXPECT_EQ(nullptr, matcher_ptr->route(*metadata_));
}

TEST_F(SipRouterTest, RouteEmptyHeader) {

  const std::string yaml = R"EOF(
             routes:
             - match:
                domain: "scscf-internal.cncs.svc.cluster.local"
                header: ""
                parameter: "x-suri"
               route:
                cluster: fake_cluster
)EOF";

  envoy::extensions::filters::network::sip_proxy::v3alpha::RouteConfiguration config;
  TestUtility::loadFromYaml(yaml, config);

  initializeMetadata(MsgType::Request);
  auto matcher_ptr = std::make_shared<RouteMatcher>(config);

  // Match domain
  metadata_->addMsgHeader(
      HeaderType::Route,
      "Route: "
      "<sip:test@pcsf-cfed.cncs.svc.cluster.local;role=anch;lr;transport=udp;x-suri="
      "sip:scscf-internal.cncs.svc.cluster.local:5060>");

  EXPECT_NE(nullptr, matcher_ptr->route(*metadata_));
}

TEST_F(SipRouterTest, RouteNoRouteHeaderUsingTopLine) {

  const std::string yaml = R"EOF(
             routes:
             - match:
                domain: "scscf-internal.cncs.svc.cluster.local"
                header: "Route"
                parameter: "x-suri"
               route:
                cluster: fake_cluster
)EOF";

  envoy::extensions::filters::network::sip_proxy::v3alpha::RouteConfiguration config;
  TestUtility::loadFromYaml(yaml, config);

  initializeMetadata(MsgType::Request);
  auto matcher_ptr = std::make_shared<RouteMatcher>(config);

  // Match domain
  metadata_->listHeader(HeaderType::Route).clear();
  metadata_->addMsgHeader(HeaderType::TopLine,
                          "INVITE sip:User.0000@scscf-internal.cncs.svc.cluster.local;ep=127.0.0.1 "
                          "SIP/2.0\x0d\x0a");

  EXPECT_NE(nullptr, matcher_ptr->route(*metadata_));
}

TEST_F(SipRouterTest, RouteUsingEmptyTopLine) {

  const std::string yaml = R"EOF(
             routes:
             - match:
                domain: "scscf-internal.cncs.svc.cluster.local"
                header: "Route"
                parameter: "x-suri"
               route:
                cluster: fake_cluster
)EOF";

  envoy::extensions::filters::network::sip_proxy::v3alpha::RouteConfiguration config;
  TestUtility::loadFromYaml(yaml, config);

  initializeMetadata(MsgType::Request);
  metadata_->listHeader(HeaderType::Route).clear();
  auto matcher_ptr = std::make_shared<RouteMatcher>(config);

  EXPECT_EQ(nullptr, matcher_ptr->route(*metadata_));
}

TEST_F(SipRouterTest, RouteUsingEmptyRecordRoute) {

  const std::string yaml = R"EOF(
             routes:
             - match:
                domain: "scscf-internal.cncs.svc.cluster.local"
                header: "Record-Route"
                parameter: "x-suri"
               route:
                cluster: fake_cluster
)EOF";

  envoy::extensions::filters::network::sip_proxy::v3alpha::RouteConfiguration config;
  TestUtility::loadFromYaml(yaml, config);

  initializeMetadata(MsgType::Request);
  auto matcher_ptr = std::make_shared<RouteMatcher>(config);

  EXPECT_EQ(nullptr, matcher_ptr->route(*metadata_));
}

TEST_F(SipRouterTest, RouteHeaderHostDomain) {

  const std::string yaml = R"EOF(
             routes:
             - match:
                domain: "pcsf-cfed.cncs.svc.cluster.local"
                header: "Route"
                parameter: "host"
               route:
                cluster: fake_cluster
)EOF";

  envoy::extensions::filters::network::sip_proxy::v3alpha::RouteConfiguration config;
  TestUtility::loadFromYaml(yaml, config);

  initializeMetadata(MsgType::Request);
  auto matcher_ptr = std::make_shared<RouteMatcher>(config);

  // Match domain
  metadata_->addMsgHeader(
      HeaderType::Route,
      "Route: "
      "<sip:test@pcsf-cfed.cncs.svc.cluster.local;role=anch;lr;transport=udp;x-suri="
      "sip:scscf-internal.cncs.svc.cluster.local:5060>");

  EXPECT_NE(nullptr, matcher_ptr->route(*metadata_));
}

TEST_F(SipRouterTest, RouteHeaderWildcardDomain) {

  const std::string yaml = R"EOF(
             routes:
             - match:
                domain: "*"
                header: "Route"
                parameter: "host"
               route:
                cluster: fake_cluster
)EOF";

  envoy::extensions::filters::network::sip_proxy::v3alpha::RouteConfiguration config;
  TestUtility::loadFromYaml(yaml, config);

  initializeMetadata(MsgType::Request);
  auto matcher_ptr = std::make_shared<RouteMatcher>(config);

  // Match domain
  metadata_->addMsgHeader(
      HeaderType::Route,
      "Route: "
      "<sip:test@pcsf-cfed.cncs.svc.cluster.local;role=anch;lr;transport=udp;x-suri="
      "sip:scscf-internal.cncs.svc.cluster.local:5060>");

  EXPECT_NE(nullptr, matcher_ptr->route(*metadata_));
}

TEST_F(SipRouterTest, Audit) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();

  auto& transaction_info_ptr = (*transaction_infos_)[cluster_name_];
  EXPECT_NE(nullptr, transaction_info_ptr);
  std::shared_ptr<UpstreamConnection> upstream_connection_ptr =
      transaction_info_ptr->getUpstreamConnection("10.0.0.1");
  EXPECT_NE(nullptr, upstream_connection_ptr);

  std::shared_ptr<TransactionInfoItem> item =
      std::make_shared<TransactionInfoItem>(&callbacks_, upstream_connection_ptr);
  std::shared_ptr<TransactionInfoItem> itemToDelete =
      std::make_shared<TransactionInfoItem>(&callbacks_, upstream_connection_ptr);
  itemToDelete->toDelete();
  ThreadLocalTransactionInfo threadInfo(transaction_info_ptr, dispatcher_,
                                        std::chrono::milliseconds(0));
  threadInfo.transaction_info_map_.emplace(cluster_name_, item);
  threadInfo.transaction_info_map_.emplace("test1", itemToDelete);
  threadInfo.auditTimerAction();
}

TEST_F(SipRouterTest, AddXEnvoyOriginIngressHeader) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request, MethodType::Invite, false);
  startRequest();
  connectUpstream();

  EXPECT_CALL(upstream_connection_, write(_, _))
      .WillOnce(Invoke([this](Buffer::Instance& buffer, bool) -> void {
        auto header_name =
            std::string(HeaderTypes::get().header2Str(HeaderType::XEnvoyOriginIngress));
        EXPECT_THAT(buffer.toString(),
                    testing::HasSubstr(header_name + ": " + origin_ingress_.toHeaderValue()));
        buffer.drain(buffer.length());
      }));

  completeRequest();
  destroyRouter();
}

TEST_F(SipRouterTest, AddXEnvoyOriginIngressHeaderWithPrevValue) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request, MethodType::Invite, false, "X-Envoy-Origin-Ingress: thread=123; downstream-connection=abc\x0d\x0a");
  startRequest();
  connectUpstream();

  EXPECT_CALL(upstream_connection_, write(_, _))
      .WillOnce(Invoke([this](Buffer::Instance& buffer, bool) -> void {
        auto header_name =
            std::string(HeaderTypes::get().header2Str(HeaderType::XEnvoyOriginIngress));
        EXPECT_THAT(buffer.toString(), testing::Not(testing::HasSubstr(
                    "X-Envoy-Origin-Ingress: thread=123; downstream-connection=abc")));
        EXPECT_THAT(buffer.toString(),
                    testing::HasSubstr(header_name + ": " + origin_ingress_.toHeaderValue()));
        buffer.drain(buffer.length());
      }));

  completeRequest();
  destroyRouter();
}

TEST_F(SipRouterTest, AddXEnvoyOriginIngressHeaderUpstreamSupportDisabled) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: false
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request, MethodType::Invite, false);
  startRequest();
  connectUpstream();

  EXPECT_CALL(upstream_connection_, write(_, _))
      .WillOnce(Invoke([this](Buffer::Instance& buffer, bool) -> void {
        auto header_name =
            std::string(HeaderTypes::get().header2Str(HeaderType::XEnvoyOriginIngress));
        EXPECT_THAT(buffer.toString(), testing::Not(
                    testing::HasSubstr(header_name + ": " + origin_ingress_.toHeaderValue())));
        buffer.drain(buffer.length());
      }));

  completeRequest();
  destroyRouter();
}

TEST_F(SipRouterTest, UpstreamRequest) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();
  completeRequest();

  EXPECT_CALL(*filter_callbacks_, upstreamData(_, _, _)).WillOnce(
    Invoke([](MessageMetadataSharedPtr metadata,
        RouteConstSharedPtr,
        const absl::optional<std::string>&) -> SipFilters::ResponseStatus {
          EXPECT_EQ(metadata->operationList().size(), 1);
          auto operation = metadata->operationList().at(0);
          EXPECT_EQ(operation.type_, OperationType::Delete);
          EXPECT_EQ(operation.position_, metadata->rawMsg().find("X-Envoy-Origin-Ingress"));
          EXPECT_EQ(metadata->listHeader(XEnvoyOriginIngress).size(), 0);
          return SipFilters::ResponseStatus::Complete;
        })
  );
  
  initUpstreamRequest(origin_ingress_.toHeaderValue());
  
  destroyRouter();
}

TEST_F(SipRouterTest, UpstreamRequestDownstreamConnectionFail) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();
  completeRequest();

  EXPECT_CALL(*filter_callbacks_, upstreamData(_, _, _)).WillOnce(
    Invoke([](MessageMetadataSharedPtr metadata,
        RouteConstSharedPtr,
        const absl::optional<std::string>&) -> SipFilters::ResponseStatus {
          EXPECT_EQ(metadata->operationList().size(), 1);
          auto operation = metadata->operationList().at(0);
          EXPECT_EQ(operation.type_, OperationType::Delete);
          EXPECT_EQ(operation.position_, metadata->rawMsg().find("X-Envoy-Origin-Ingress"));
          EXPECT_EQ(metadata->listHeader(XEnvoyOriginIngress).size(), 0);
          return SipFilters::ResponseStatus::Reset;
        })
  );
  
  initUpstreamRequest(origin_ingress_.toHeaderValue());
  
  destroyRouter();
}

TEST_F(SipRouterTest, UpstreamRequestUpstreamSupportDisabled) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: false
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();
  completeRequest();

  EXPECT_CALL(*filter_callbacks_, upstreamData(_, _, _)).Times(0);
  EXPECT_LOG_CONTAINS("error", "Upstream transaction support disabled", initUpstreamRequest(origin_ingress_.toHeaderValue()));
  
  destroyRouter();
}

TEST_F(SipRouterTest, UpstreamRequestInvalidMessage) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();
  completeRequest();
  
  Buffer::OwnedImpl buffer;
  const std::string SIP_INVITE =
        "INVITE sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
        "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
        "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
        "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
        "Content-Type: application/sdp\x0d\x0a" 
        "Content-Length:  0\x0d\x0a"
        "\x0d\x0a";
  buffer.add(SIP_INVITE);

  EXPECT_CALL(*filter_callbacks_, upstreamData(_, _, _)).Times(0);
  EXPECT_LOG_CONTAINS("error", "Dropping upstream request with no well formatted header", initUpstreamRequest(buffer));
  
  destroyRouter();
}

TEST_F(SipRouterTest, UpstreamRequestNoXEnvoyOriginIngressHeader) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();
  completeRequest();

  EXPECT_CALL(*filter_callbacks_, upstreamData(_, _, _)).Times(0);
  EXPECT_LOG_CONTAINS("error", "Dropping upstream request with no X-Envoy-Origin-Ingress header", initUpstreamRequest(""));
  
  destroyRouter();
}

TEST_F(SipRouterTest, UpstreamRequestInvalidXEnvoyOriginIngressHeader) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();
  completeRequest();

  EXPECT_CALL(*filter_callbacks_, upstreamData(_, _, _)).Times(0);
  EXPECT_LOG_CONTAINS("error", "Dropping upstream request with invalid format of X-Envoy-Origin-Ingress header", initUpstreamRequest("invalid"));
  
  destroyRouter();
}

TEST_F(SipRouterTest, UpstreamRequestWrongThread) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();
  completeRequest();

  EXPECT_CALL(*filter_callbacks_, upstreamData(_, _, _)).Times(0);
  EXPECT_LOG_CONTAINS("error", "Thread ID error received different from local thread ID", initUpstreamRequest("thread=error; downstream-connection=xyz"));
  
  destroyRouter();
}

TEST_F(SipRouterTest, UpstreamRequestWrongDownstreamConn) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();
  completeRequest();

  EXPECT_CALL(*filter_callbacks_, upstreamData(_, _, _)).Times(0);
  EXPECT_LOG_CONTAINS("error", "No downstream connection found for error", initUpstreamRequest("thread=" + origin_ingress_.getThreadID() + "; downstream-connection=error"));
  
  destroyRouter();
}

TEST_F(SipRouterTest, ResponseToUpstreamRequestInNewConnection) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();

  initializeMetadataResponse();

  startResponse();
  connectUpstream();

  EXPECT_CALL(upstream_connection_, write(_, _))
      .WillOnce(Invoke([](Buffer::Instance& buffer, bool) -> void {
        EXPECT_THAT(buffer.toString(),
                    testing::HasSubstr("SIP/2.0 200 OK"));
        buffer.drain(buffer.length());
      }));
      
  completeResponse();

  destroyRouter();
}

TEST_F(SipRouterTest, ResponseToUpstreamRequestEmptyDestination) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();

  initializeMetadataResponse(MethodType::Ok200, false);

  startResponse();
  connectUpstream();

  EXPECT_CALL(upstream_connection_, write(_, _))
      .WillOnce(Invoke([](Buffer::Instance& buffer, bool) -> void {
        EXPECT_THAT(buffer.toString(),
                    testing::HasSubstr("SIP/2.0 200 OK"));
        buffer.drain(buffer.length());
      }));

  completeResponse();
  destroyRouter();
}

TEST_F(SipRouterTest, ResponseToUpstreamRequestNoHost) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();

  initializeMetadataResponse();

  EXPECT_CALL(upstream_connection_, write(_, _)).Times(0);


  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillOnce(ReturnRef(cluster_name_));
  EXPECT_EQ(FilterStatus::Continue, router_->transportBegin(metadata_));

  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_, host())
      .WillOnce(Return(nullptr));
  EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));

  destroyRouter();
}

TEST_F(SipRouterTest, ResponseToUpstreamRequestInConnectedUpstreamConnection) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();
  completeRequest();
  destroyRouter();
  
  initUpstreamRequest(origin_ingress_.toHeaderValue());

  initializeRouter();
  initializeTransaction();
  initializeTransaction();
  initializeMetadataResponse();
  startResponse(FilterStatus::Continue);
  
  destroyRouter();
}

TEST_F(SipRouterTest, ResponseToUpstreamRequestInConnectingUpstreamConnection) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: true
)EOF";
  initializeTrans(sip_protocol_options_yaml);

  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  
  startResponse(FilterStatus::StopIteration);
  
  destroyRouter();
}

TEST_F(SipRouterTest, LocalReplyToUpstreamRequest) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: false
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();
  completeRequest();

  EXPECT_CALL(*filter_callbacks_, upstreamData(_, _, _)).Times(0);
  EXPECT_LOG_CONTAINS("error", "Upstream transaction support disabled", initUpstreamRequest(origin_ingress_.toHeaderValue()));
  
  auto& transaction_info_ptr = (*transaction_infos_)[cluster_name_];
  std::shared_ptr<UpstreamConnection> upstream_connection_ptr =
      transaction_info_ptr->getUpstreamConnection("10.0.0.1");
  EXPECT_NE(nullptr, upstream_connection_ptr);

  EXPECT_CALL(upstream_connection_, write(_, _))
      .WillOnce(Invoke([](Buffer::Instance& buffer, bool) -> void {
        EXPECT_THAT(buffer.toString(),
                    testing::HasSubstr("SIP/2.0 503 Service Unavailable"));
        EXPECT_THAT(buffer.toString(),
                    testing::HasSubstr("Reason: Testing upstream local replies"));
        buffer.drain(buffer.length());
      }));

  upstream_connection_ptr->onError(metadata_, ErrorCode::ServiceUnavailable, "Testing upstream local replies");
  
  destroyRouter();
}

TEST_F(SipRouterTest, LocalReplyToUpstreamRequestConnectionClosed) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: false
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();
  completeRequest();

  EXPECT_CALL(*filter_callbacks_, upstreamData(_, _, _)).Times(0);
  EXPECT_LOG_CONTAINS("error", "Upstream transaction support disabled", initUpstreamRequest(origin_ingress_.toHeaderValue()));
  
  auto& transaction_info_ptr = (*transaction_infos_)[cluster_name_];
  std::shared_ptr<UpstreamConnection> upstream_connection_ptr =
      transaction_info_ptr->getUpstreamConnection("10.0.0.1");
  EXPECT_NE(nullptr, upstream_connection_ptr);

  EXPECT_CALL(upstream_connection_, write(_, _)).Times(0);

  upstream_connection_ptr->releaseConnection(true);
  upstream_connection_ptr->onError(metadata_, ErrorCode::ServiceUnavailable, "Testing upstream local replies");
  
  destroyRouter();
}

TEST_F(SipRouterTest, UpstreamConnectionReset) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        upstream_transactions:
          enabled: false
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);
  startRequest();
  connectUpstream();
  
  auto& transaction_info_ptr = (*transaction_infos_)[cluster_name_];
  std::shared_ptr<UpstreamConnection> upstream_connection_ptr =
      transaction_info_ptr->getUpstreamConnection("10.0.0.1");
  EXPECT_NE(nullptr, upstream_connection_ptr);

  EXPECT_THROW(upstream_connection_ptr->onResetStream(ConnectionPool::PoolFailureReason::Overflow),
               AppException);
  upstream_connection_ptr->onResetStream(ConnectionPool::PoolFailureReason::LocalConnectionFailure);
  upstream_connection_ptr->onResetStream(ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  upstream_connection_ptr->onResetStream(ConnectionPool::PoolFailureReason::Timeout);
  
  destroyRouter();
}

TEST_F(SipRouterTest, HandleAffinityNoAffinityConfigured) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: false
        registration_affinity: false
        upstream_transactions:
          enabled: false
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();

  initializeMetadata(MsgType::Request, MethodType::Invite, false);
  metadata_->setPCookieIpMap({"S1F1", "10.0.0.1"});
  metadata_->affinity().clear();
  metadata_->removeMsgHeader(HeaderType::Route);
  metadata_->addMsgHeader(
      HeaderType::Route,
      "Route: "
      "<sip:test@pcsf-cfed.cncs.svc.cluster.local;role=anch;lr;transport=udp;x-suri="
      "sip:scscf-internal.cncs.svc.cluster.local:5060;ep=10.0.0.1>");

  startRequest();
  
  EXPECT_EQ(metadata_->affinity().size(), 0);

  destroyRouter();
}

TEST_F(SipRouterTest, HandleAffinityNoAffinityConfiguredForceNoLoadBalancer) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: false
        registration_affinity: false
        customized_affinity:
          stop_load_balance: true
        upstream_transactions:
          enabled: false
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();

  initializeMetadata(MsgType::Request, MethodType::Invite, false);
  metadata_->setPCookieIpMap({"S1F1", "10.0.0.1"});
  metadata_->affinity().clear();

  EXPECT_CALL(callbacks_, route()).WillRepeatedly(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillRepeatedly(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_EQ(FilterStatus::Continue, router_->transportBegin(metadata_));

  EXPECT_THROW(router_->messageBegin(metadata_), AppException);
  
  EXPECT_EQ(metadata_->affinity().size(), 0);

  destroyRouter();
}

TEST_F(SipRouterTest, HandleAffinityNoAffinityConfiguredForceLoadBalancer) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: false
        registration_affinity: false
        customized_affinity:
          stop_load_balance: false
        upstream_transactions:
          enabled: false
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();

  initializeMetadata(MsgType::Request, MethodType::Invite, false);
  metadata_->setPCookieIpMap({"S1F1", "10.0.0.1"});
  metadata_->affinity().clear();

  startRequest();
  
  EXPECT_EQ(metadata_->affinity().size(), 0);

  destroyRouter();
}

TEST_F(SipRouterTest, HandleAffinitySessionAffinityConfigured) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: false
        upstream_transactions:
          enabled: false
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();

  initializeMetadata(MsgType::Request, MethodType::Invite, false);
  metadata_->setPCookieIpMap({"S1F1", "10.0.0.1"});
  metadata_->affinity().clear();

  startRequest();
  
  EXPECT_EQ(metadata_->affinity().size(), 1);
  EXPECT_EQ(metadata_->affinityIteration()->header(), "Route");
  EXPECT_EQ(metadata_->affinityIteration()->type(), "ep");
  EXPECT_EQ(metadata_->affinityIteration()->key(), "ep");
  EXPECT_EQ(metadata_->affinityIteration()->query(), false);
  EXPECT_EQ(metadata_->affinityIteration()->subscribe(), false);

  destroyRouter();
}

TEST_F(SipRouterTest, HandleAffinitySessionAffinityConfiguredWithRegisterMsg) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: false
        upstream_transactions:
          enabled: false
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();

  initializeMetadata(MsgType::Request, MethodType::Register, false);
  metadata_->setPCookieIpMap({"S1F1", "10.0.0.1"});
  metadata_->affinity().clear();

  startRequest();
  
  EXPECT_EQ(metadata_->affinity().size(), 0);

  destroyRouter();
}

TEST_F(SipRouterTest, HandleAffinityRegistrationAffinityConfigured) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: false
        registration_affinity: true
        upstream_transactions:
          enabled: false
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();

  initializeMetadata(MsgType::Request, MethodType::Register, false);
  metadata_->setPCookieIpMap({"S1F1", "10.0.0.1"});
  metadata_->affinity().clear();
  metadata_->setOpaque("10.0.0.1");

  startRequest();
  
  EXPECT_EQ(metadata_->affinity().size(), 1);
  EXPECT_EQ(metadata_->affinityIteration()->header(), "Route");
  EXPECT_EQ(metadata_->affinityIteration()->type(), "ep");
  EXPECT_EQ(metadata_->affinityIteration()->key(), "ep");
  EXPECT_EQ(metadata_->affinityIteration()->query(), false);
  EXPECT_EQ(metadata_->affinityIteration()->subscribe(), false);

  destroyRouter();
}

TEST_F(SipRouterTest, HandleAffinityRegistrationAffinityConfiguredWithNonRegisterMsg) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: false
        registration_affinity: true
        upstream_transactions:
          enabled: false
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();

  initializeMetadata(MsgType::Request, MethodType::Invite, false);
  metadata_->setPCookieIpMap({"S1F1", "10.0.0.1"});
  metadata_->affinity().clear();
  metadata_->setOpaque("10.0.0.1");

  startRequest();
  
  EXPECT_EQ(metadata_->affinity().size(), 0);

  destroyRouter();
}

TEST_F(SipRouterTest, HandleAffinityCustomizedAffinityConfigured) {
  const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: false
        registration_affinity: false
        customized_affinity:
          entries:
          - key_name: ep
            query: false
            subscribe: false
          - header: Test
            key_name: header_test
            query: true
            subscribe: true
          - header: Path
            key_name: header_route
            query: false
            subscribe: true
          - key_name: text
            query: true
            subscribe: true
          - header: X-Envoy-Origin-Ingress
            key_name: text
            query: true
            subscribe: false
        upstream_transactions:
          enabled: false
)EOF";
  initializeTrans(sip_protocol_options_yaml);
  initializeRouter();
  initializeTransaction();

  initializeMetadata(MsgType::Request, MethodType::Invite, false);
  metadata_->setPCookieIpMap({"S1F1", "10.0.0.1"});
  metadata_->affinity().clear();
  metadata_->addMsgHeader(
      HeaderType::TopLine,
      "INVITE sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0<lskpmc=10.0.0.1>");
  metadata_->removeMsgHeader(HeaderType::Route);
  metadata_->addMsgHeader(
      HeaderType::Path,
      "Path: <sip:10.177.8.232;x-fbi=cfed;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;inst-ip=192.169.110.53;lr;ottag=ue_term;bidx=563242011197570;access-type=ADSL;x-alu-prset-id;header_route=10.0.0.1>");
  metadata_->addMsgHeader(
      HeaderType::XEnvoyOriginIngress,"XEnvoyOriginIngress");

  EXPECT_CALL(callbacks_, route()).WillRepeatedly(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillRepeatedly(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_EQ(FilterStatus::Continue, router_->transportBegin(metadata_));
  
  EXPECT_EQ(metadata_->affinity().size(), 3);
  EXPECT_EQ(metadata_->affinityIteration()->header(), "");
  EXPECT_EQ(metadata_->affinityIteration()->type(), "ep");
  EXPECT_EQ(metadata_->affinityIteration()->key(), "ep");
  EXPECT_EQ(metadata_->affinityIteration()->query(), false);
  EXPECT_EQ(metadata_->affinityIteration()->subscribe(), false);
  metadata_->nextAffinityIteration();
  EXPECT_EQ(metadata_->affinityIteration()->header(), "Path");
  EXPECT_EQ(metadata_->affinityIteration()->type(), "header_route");
  EXPECT_EQ(metadata_->affinityIteration()->key(), "10.0.0.1");
  EXPECT_EQ(metadata_->affinityIteration()->query(), false);
  EXPECT_EQ(metadata_->affinityIteration()->subscribe(), true);
  metadata_->nextAffinityIteration();
  EXPECT_EQ(metadata_->affinityIteration()->header(), "X-Envoy-Origin-Ingress");
  EXPECT_EQ(metadata_->affinityIteration()->type(), "text");
  EXPECT_EQ(metadata_->affinityIteration()->key(), "XEnvoyOriginIngress");
  EXPECT_EQ(metadata_->affinityIteration()->query(), true);
  EXPECT_EQ(metadata_->affinityIteration()->subscribe(), false);

  destroyRouter();
}

} // namespace Router
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
