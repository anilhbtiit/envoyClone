#pragma once

#include <memory>

#include "envoy/http/conn_pool.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/router/router_ratelimit.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/tcp/upstream.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/thread_local_cluster.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/dump_state_utils.h"
#include "source/common/http/codec_client.h"
#include "source/common/http/hash_policy.h"
#include "source/common/network/utility.h"
#include "source/common/http/null_route_impl.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/header_parser.h"
#include "source/common/router/router.h"
#include "source/extensions/early_data/default_early_data_policy.h"

namespace Envoy {
namespace TcpProxy {

constexpr absl::string_view DisableTunnelingFilterStateKey = "envoy.tcp_proxy.disable_tunneling";

class TcpConnPool : public GenericConnPool, public Tcp::ConnectionPool::Callbacks {
public:
  TcpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
              Upstream::LoadBalancerContext* context,
              Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks,
              StreamInfo::StreamInfo& downstream_info);
  ~TcpConnPool() override;

  bool valid() const { return conn_pool_data_.has_value(); }

  // GenericConnPool
  void newStream(GenericConnectionPoolCallbacks& callbacks) override;

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                   Upstream::HostDescriptionConstSharedPtr host) override;

private:
  absl::optional<Upstream::TcpPoolData> conn_pool_data_{};
  Tcp::ConnectionPool::Cancellable* upstream_handle_{};
  GenericConnectionPoolCallbacks* callbacks_{};
  Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks_;
  StreamInfo::StreamInfo& downstream_info_;
};

class HttpUpstream;

class HttpConnPool : public GenericConnPool, public Http::ConnectionPool::Callbacks {
public:
  HttpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
               Upstream::LoadBalancerContext* context, const TunnelingConfigHelper& config,
               Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks,
               Http::StreamDecoderFilterCallbacks&, Http::CodecType type,
               StreamInfo::StreamInfo& downstream_info);

  using RouterUpstreamRequest = Router::UpstreamRequest;
  using RouterUpstreamRequestPtr = std::unique_ptr<RouterUpstreamRequest>;
  ~HttpConnPool() override;

  bool valid() const { return conn_pool_data_.has_value() || generic_conn_pool_; }
  Http::CodecType codecType() const { return type_; }
  std::unique_ptr<Router::GenericConnPool> createConnPool(Upstream::ThreadLocalCluster&,
                                                          Upstream::LoadBalancerContext* context,
                                                          absl::optional<Http::Protocol> protocol);

  // GenericConnPool
  void newStream(GenericConnectionPoolCallbacks& callbacks) override;

  // Http::ConnectionPool::Callbacks,
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Http::RequestEncoder& request_encoder,
                   Upstream::HostDescriptionConstSharedPtr host, StreamInfo::StreamInfo& info,
                   absl::optional<Http::Protocol>) override;

  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr);
  void onHttpPoolReady(Upstream::HostDescriptionConstSharedPtr& host,
                       Ssl::ConnectionInfoConstSharedPtr ssl_info);

  class Callbacks {
  public:
    Callbacks(HttpConnPool& conn_pool, Upstream::HostDescriptionConstSharedPtr host,
              Ssl::ConnectionInfoConstSharedPtr ssl_info)
        : conn_pool_(&conn_pool), host_(host), ssl_info_(ssl_info) {}
    virtual ~Callbacks() = default;
    virtual void onSuccess(Http::RequestEncoder* request_encoder) {
      ASSERT(conn_pool_ != nullptr);
      if (request_encoder != nullptr) {
        conn_pool_->onGenericPoolReady(host_, request_encoder->getStream().connectionInfoProvider(),
                                       ssl_info_);
      }
    }
    virtual void onFailure() {
      ASSERT(conn_pool_ != nullptr);
      conn_pool_->callbacks_->onGenericPoolFailure(
          ConnectionPool::PoolFailureReason::RemoteConnectionFailure, "", host_);
    }

  protected:
    Callbacks() = default;

  private:
    HttpConnPool* conn_pool_{};
    Upstream::HostDescriptionConstSharedPtr host_;
    Ssl::ConnectionInfoConstSharedPtr ssl_info_;
  };

private:
  void onGenericPoolReady(Upstream::HostDescriptionConstSharedPtr& host,
                          const Network::ConnectionInfoProvider& address_provider,
                          Ssl::ConnectionInfoConstSharedPtr ssl_info);
  const TunnelingConfigHelper& config_;
  Http::CodecType type_;
  absl::optional<Upstream::HttpPoolData> conn_pool_data_{};
  Http::ConnectionPool::Cancellable* upstream_handle_{};
  GenericConnectionPoolCallbacks* callbacks_{};
  Http::StreamDecoderFilterCallbacks* decoder_filter_callbacks_;
  Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks_;
  std::unique_ptr<HttpUpstream> upstream_;
  StreamInfo::StreamInfo& downstream_info_;
  std::unique_ptr<Router::GenericConnPool> generic_conn_pool_;
  std::shared_ptr<Http::NullRouteImpl> route_;
};

class TcpUpstream : public GenericUpstream {
public:
  TcpUpstream(Tcp::ConnectionPool::ConnectionDataPtr&& data,
              Tcp::ConnectionPool::UpstreamCallbacks& callbacks);

  // GenericUpstream
  bool readDisable(bool disable) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void addBytesSentCallback(Network::Connection::BytesSentCb cb) override;
  Tcp::ConnectionPool::ConnectionData* onDownstreamEvent(Network::ConnectionEvent event) override;
  bool startUpstreamSecureTransport() override;
  Ssl::ConnectionInfoConstSharedPtr getUpstreamConnectionSslInfo() override;

private:
  Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;
};

class HttpUpstream : public GenericUpstream,
                     public Envoy::Router::RouterFilterInterface,
                     protected Http::StreamCallbacks {
public:
  using TunnelingConfig =
      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_TunnelingConfig;

  using UpstreamRequest = Router::UpstreamRequest;
  using UpstreamRequestPtr = std::unique_ptr<UpstreamRequest>;

  ~HttpUpstream() override;
  virtual void setRouterUpstreamRequest(UpstreamRequestPtr) PURE;
  virtual void newStream(GenericConnectionPoolCallbacks& callbacks) PURE;
  virtual bool isValidResponse(const Http::ResponseHeaderMap&) PURE;

  void doneReading();
  void doneWriting();
  void cleanUp();
  Http::ResponseDecoder& responseDecoder() { return response_decoder_; }

  // GenericUpstream
  bool readDisable(bool disable) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void addBytesSentCallback(Network::Connection::BytesSentCb cb) override;
  Tcp::ConnectionPool::ConnectionData* onDownstreamEvent(Network::ConnectionEvent event) override;
  // HTTP upstream must not implement converting upstream transport
  // socket from non-secure to secure mode.
  bool startUpstreamSecureTransport() override { return false; }

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason,
                     absl::string_view transport_failure_reason) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

  virtual void setRequestEncoder(Http::RequestEncoder& request_encoder, bool is_ssl) PURE;
  void setConnPoolCallbacks(std::unique_ptr<HttpConnPool::Callbacks>&& callbacks) {
    conn_pool_callbacks_ = std::move(callbacks);
  }
  Ssl::ConnectionInfoConstSharedPtr getUpstreamConnectionSslInfo() override { return nullptr; }

protected:
  HttpUpstream(HttpConnPool& http_conn_pool, Http::StreamDecoderFilterCallbacks& decoder_callbacks,
               Router::Route& route, Tcp::ConnectionPool::UpstreamCallbacks& callbacks,
               const TunnelingConfigHelper& config, StreamInfo::StreamInfo& downstream_info);
  void virtual resetEncoder(Network::ConnectionEvent event, bool inform_downstream = true);
  void onResetEncoder(Network::ConnectionEvent event, bool inform_downstream = true);

  // The encoder offered by the upstream http client.
  Http::RequestEncoder* request_encoder_{};
  // The config object that is owned by the downstream network filter chain factory.
  const TunnelingConfigHelper& config_;
  // The downstream info that is owned by the downstream connection.
  StreamInfo::StreamInfo& downstream_info_;
  // Router::UpstreamRequest instances which are owned by this HttpUpstream.
  std::list<UpstreamRequestPtr> upstream_requests_;
  std::unique_ptr<Http::RequestHeaderMapImpl> downstream_headers_;
  HttpConnPool& parent_;

private:
  // Router::RouterFilterInterface
  void onUpstreamHeaders(uint64_t response_code, Http::ResponseHeaderMapPtr&& headers,
                         UpstreamRequest& upstream_request, bool end_stream) override;
  void onUpstreamData(Buffer::Instance& data, UpstreamRequest& upstream_request,
                      bool end_stream) override;
  void onUpstream1xxHeaders(Http::ResponseHeaderMapPtr&&, UpstreamRequest&) override {}
  void onUpstreamTrailers(Http::ResponseTrailerMapPtr&&, UpstreamRequest&) override;
  void onUpstreamMetadata(Http::MetadataMapPtr&&) override {}
  void onUpstreamReset(Http::StreamResetReason stream_reset_reason,
                       absl::string_view transport_failure_reason, UpstreamRequest&) override;
  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) override {
    parent_.onUpstreamHostSelected(host);
  }
  void onPerTryTimeout(UpstreamRequest&) override {}
  void onPerTryIdleTimeout(UpstreamRequest&) override {}
  void onStreamMaxDurationReached(UpstreamRequest&) override {}
  Http::StreamDecoderFilterCallbacks* callbacks() override { return &decoder_filter_callbacks_; }
  Upstream::ClusterInfoConstSharedPtr cluster() override {
    return decoder_filter_callbacks_.clusterInfo();
  }
  Router::FilterConfig& config() override {
    return const_cast<Router::FilterConfig&>(config_.routerFilterConfig());
  }
  Router::FilterUtility::TimeoutData timeout() override { return {}; }
  absl::optional<std::chrono::milliseconds> dynamicMaxStreamDuration() const override {
    return absl::nullopt;
  }
  Http::RequestHeaderMap* downstreamHeaders() override;
  Http::RequestTrailerMap* downstreamTrailers() override { return nullptr; }
  bool downstreamResponseStarted() const override { return false; }
  bool downstreamEndStream() const override { return false; }
  uint32_t attemptCount() const override { return 0; }
  const Router::VirtualCluster* requestVcluster() const override { return nullptr; }
  const Router::RouteStatsContextOptRef routeStatsContext() const override {
    return Router::RouteStatsContextOptRef();
  }
  const Router::Route* route() const override { return route_; }
  const std::list<UpstreamRequestPtr>& upstreamRequests() const override {
    // static const std::list<UpstreamRequestPtr> requests;
    return upstream_requests_;
  }
  const UpstreamRequest* finalUpstreamRequest() const override { return nullptr; }
  TimeSource& timeSource() override { return config().timeSource(); }

  Upstream::ClusterInfoConstSharedPtr cluster_;
  Http::StreamDecoderFilterCallbacks& decoder_filter_callbacks_;
  Router::Route* route_;
  class DecoderShim : public Http::ResponseDecoder {
  public:
    DecoderShim(HttpUpstream& parent) : parent_(parent) {}
    // Http::ResponseDecoder
    void decode1xxHeaders(Http::ResponseHeaderMapPtr&&) override {}
    void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override {
      bool is_valid_response = parent_.isValidResponse(*headers);
      parent_.config_.propagateResponseHeaders(std::move(headers),
                                               parent_.downstream_info_.filterState());
      if (!is_valid_response || end_stream) {
        parent_.resetEncoder(Network::ConnectionEvent::LocalClose);
      } else if (parent_.conn_pool_callbacks_ != nullptr) {
        parent_.conn_pool_callbacks_->onSuccess(parent_.request_encoder_);
        parent_.conn_pool_callbacks_.reset();
      }
    }
    void decodeData(Buffer::Instance& data, bool end_stream) override {
      parent_.upstream_callbacks_.onUpstreamData(data, end_stream);
      if (end_stream) {
        parent_.doneReading();
      }
    }
    void decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) override {
      parent_.config_.propagateResponseTrailers(std::move(trailers),
                                                parent_.downstream_info_.filterState());
      if (Runtime::runtimeFeatureEnabled(
              "envoy.reloadable_features.finish_reading_on_decode_trailers")) {
        parent_.doneReading();
      }
    }
    void decodeMetadata(Http::MetadataMapPtr&&) override {}
    void dumpState(std::ostream& os, int indent_level) const override {
      DUMP_STATE_UNIMPLEMENTED(DecoderShim);
    }

  private:
    HttpUpstream& parent_;
  };
  DecoderShim response_decoder_;
  Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks_;
  bool read_half_closed_{};
  bool write_half_closed_{};

  // Used to defer onGenericPoolReady and onGenericPoolFailure to the reception
  // of the CONNECT response or the resetEncoder.
  std::unique_ptr<HttpConnPool::Callbacks> conn_pool_callbacks_;
};

class Http1Upstream : public HttpUpstream {
public:
  Http1Upstream(HttpConnPool& http_conn_pool, Tcp::ConnectionPool::UpstreamCallbacks& callbacks,
                Http::StreamDecoderFilterCallbacks& decoder_callbacks, Router::Route& route,
                const TunnelingConfigHelper& config, StreamInfo::StreamInfo& downstream_info);
  ~Http1Upstream() override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void setRequestEncoder(Http::RequestEncoder& request_encoder, bool is_ssl) override;
  bool isValidResponse(const Http::ResponseHeaderMap& headers) override;
  void newStream(GenericConnectionPoolCallbacks&) override {}
  void setRouterUpstreamRequest(UpstreamRequestPtr) override {}
};

class Http2Upstream : public HttpUpstream {
public:
  Http2Upstream(HttpConnPool& http_conn_pool, Tcp::ConnectionPool::UpstreamCallbacks& callbacks,
                Http::StreamDecoderFilterCallbacks& decoder_callbacks, Router::Route& route,
                const TunnelingConfigHelper& config, StreamInfo::StreamInfo& downstream_info);
  ~Http2Upstream() override;
  void setRequestEncoder(Http::RequestEncoder& request_encoder, bool is_ssl) override;
  bool isValidResponse(const Http::ResponseHeaderMap& headers) override;
  void newStream(GenericConnectionPoolCallbacks&) override {}
  void setRouterUpstreamRequest(UpstreamRequestPtr) override {}
};

// This class interface with Router::UpstreamRequest instance which is intialized
// through the base class HttpUpstream::newStream() method. This class handles both
// HTTP/1.1 and HTTP/2 upstream requests transparently and delegates the actual work to
// Router::UpstreamRequest instance.
class CombinedUpstream : public HttpUpstream {
public:
  CombinedUpstream(HttpConnPool& http_conn_pool, Tcp::ConnectionPool::UpstreamCallbacks& callbacks,
                   Http::StreamDecoderFilterCallbacks& decoder_callbacks, Router::Route& route,
                   const TunnelingConfigHelper& config, StreamInfo::StreamInfo& downstream_info);
  ~CombinedUpstream() override;
  void setRouterUpstreamRequest(UpstreamRequestPtr) override;
  void newStream(GenericConnectionPoolCallbacks& callbacks) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  Tcp::ConnectionPool::ConnectionData* onDownstreamEvent(Network::ConnectionEvent event) override;
  void setRequestEncoder(Http::RequestEncoder&, bool) override {}
  bool isValidResponse(const Http::ResponseHeaderMap&) override;
  void resetEncoder(Network::ConnectionEvent event, bool inform_downstream = true) override;
  bool readDisable(bool disable) override;
};

} // namespace TcpProxy
} // namespace Envoy
