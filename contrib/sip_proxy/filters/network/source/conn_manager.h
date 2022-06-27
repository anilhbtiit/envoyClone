#pragma once

#include "envoy/common/pure.h"
#include "envoy/common/random_generator.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/timespan.h"
#include "envoy/upstream/upstream.h"

#include "router/router.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/config/utility.h"
#include "source/common/stats/timespan_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/common/common/random_generator.h"

#include "absl/types/any.h"
#include "contrib/sip_proxy/filters/network/source/decoder.h"
#include "contrib/sip_proxy/filters/network/source/filters/filter.h"
#include "contrib/sip_proxy/filters/network/source/stats.h"
#include "contrib/sip_proxy/filters/network/source/tra/tra_impl.h"
#include "contrib/sip_proxy/filters/network/source/utility.h"
#include "metadata.h"
#include <cstddef>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

/**
 * Config is a configuration interface for ConnectionManager.
 */
class SipSettings;
class Config {
public:
  virtual ~Config() = default;

  virtual SipFilters::FilterChainFactory& filterFactory() PURE;
  virtual SipFilterStats& stats() PURE;
  virtual Router::Config& routerConfig() PURE;
  virtual std::shared_ptr<SipSettings> settings() PURE;
};

class DownstreamConnectionInfoItem;
class ConnectionManager;


// Thread local wrapper aorund our map conn-id -> DownstreamConnectionInfoItem
struct ThreadLocalDownstreamConnectionInfo : public ThreadLocal::ThreadLocalObject,
                                    public Logger::Loggable<Logger::Id::filter> {
  ThreadLocalDownstreamConnectionInfo(std::shared_ptr<SipFilters::DownstreamConnectionInfos> parent)
      : parent_(parent) {
  }
  absl::flat_hash_map<std::string, std::shared_ptr<SipFilters::DecoderFilterCallbacks>> downstream_connection_info_map_{};

  std::shared_ptr<SipFilters::DownstreamConnectionInfos> parent_;
};

//, Logger::Loggable<Logger::Id::connection>
class DownstreamConnectionInfosImpl: SipFilters::DownstreamConnectionInfos {
public:
   DownstreamConnectionInfosImpl(ThreadLocal::SlotAllocator& tls)
      : tls_(tls.allocateSlot()){}

  // init one threadlocal map per worker thread
  void init() override {
    // Note: `this` and `cluster_name` have a lifetime of the filter.
    // That may be shorter than the tls callback if the listener is torn down shortly after it is
    // created. We use a weak pointer to make sure this object outlives the tls callbacks.
    std::weak_ptr<SipFilters::DownstreamConnectionInfos> this_weak_ptr = this->shared_from_this();
    tls_->set(
        [this_weak_ptr](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
          UNREFERENCED_PARAMETER(dispatcher); // todo
          if (auto this_shared_ptr = this_weak_ptr.lock()) {
            return std::make_shared<ThreadLocalDownstreamConnectionInfo>(this_shared_ptr);
          }
          return nullptr;
        });
  }
  ~DownstreamConnectionInfosImpl() override  = default;

  void insertDownstreamConnection(std::string conn_id,
                         Network::Connection* conn) override {
    UNREFERENCED_PARAMETER(conn);
    std::cerr << " POINTER DOWNSTREAM CONN MAP " << &(tls_->getTyped<ThreadLocalDownstreamConnectionInfo>().downstream_connection_info_map_) << std::endl;

    if (hasDownstreamConnection(conn_id)) {
      //ENVOY_LOG(info, "XXXXX NOT Inserting {} {}",  conn_id);
      std::cerr << "XXXXX Not Inserting " << conn_id << std::endl;
      return;
    }
    //ENVOY_LOG(info, "XXXXX Inserting {} {}",  conn_id);
    std::cerr << "Contents before Inserting " << conn_id << std::endl;
    for (const auto& i : tls_->getTyped<ThreadLocalDownstreamConnectionInfo>().downstream_connection_info_map_) {
      std::cerr << "Item " << i.first << " ->  " << i.second << std::endl;
    }
    // // update the map in the local thread
    // tls_->getTyped<ThreadLocalDownstreamConnectionInfo>().downstream_connection_info_map_.emplace(std::make_pair(
    //     conn_id, std::make_shared<DownstreamConnectionInfoItem>(conn)));

    std::cerr << "Contents after Inserting " << conn_id << std::endl;
    for (const auto& i : tls_->getTyped<ThreadLocalDownstreamConnectionInfo>().downstream_connection_info_map_) {
      std::cerr << "XXY " << i.first << " -> " << i.second << std::endl;
    }
  }

  size_t size() override {
    return tls_->getTyped<ThreadLocalDownstreamConnectionInfo>().downstream_connection_info_map_.size();
  }

  void deleteDownstreamConnection(std::string&& conn_id) override {
    if (hasDownstreamConnection(conn_id)) {
      // fixme - probably need to have this run on the main thread?
      tls_->getTyped<ThreadLocalDownstreamConnectionInfo>().downstream_connection_info_map_.erase(conn_id);
    }
  }

  bool hasDownstreamConnection(std::string& conn_id) override {
    return tls_->getTyped<ThreadLocalDownstreamConnectionInfo>().downstream_connection_info_map_.contains(conn_id);
  }

  SipFilters::DecoderFilterCallbacks& getDownstreamConnection(std::string& conn_id) override {
    return *(tls_->getTyped<ThreadLocalDownstreamConnectionInfo>().downstream_connection_info_map_.at(conn_id));
  }

private:
  ThreadLocal::SlotPtr tls_;
};

/**
 * Extends Upstream::ProtocolOptionsConfig with Sip-specific cluster options.
 */
class ProtocolOptionsConfig : public Upstream::ProtocolOptionsConfig {
public:
  ~ProtocolOptionsConfig() override = default;

  virtual bool sessionAffinity() const PURE;
  virtual bool registrationAffinity() const PURE;
  virtual const envoy::extensions::filters::network::sip_proxy::v3alpha::CustomizedAffinity&
  customizedAffinity() const PURE;
};

class ConnectionManager;
class TrafficRoutingAssistantHandler : public TrafficRoutingAssistant::RequestCallbacks,
                                       public Logger::Loggable<Logger::Id::filter> {
public:
  TrafficRoutingAssistantHandler(
      ConnectionManager& parent, Event::Dispatcher& dispatcher,
      const envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceConfig& config,
      Server::Configuration::FactoryContext& context, StreamInfo::StreamInfoImpl& stream_info);

  virtual void updateTrafficRoutingAssistant(const std::string& type, const std::string& key,
                                             const std::string& val,
                                             const absl::optional<TraContextMap> context);
  virtual QueryStatus retrieveTrafficRoutingAssistant(
      const std::string& type, const std::string& key, const absl::optional<TraContextMap> context,
      SipFilters::DecoderFilterCallbacks& activetrans, std::string& host);
  virtual void deleteTrafficRoutingAssistant(const std::string& type, const std::string& key,
                                             const absl::optional<TraContextMap> context);
  virtual void subscribeTrafficRoutingAssistant(const std::string& type);
  void complete(const TrafficRoutingAssistant::ResponseType& type, const std::string& message_type,
                const absl::any& resp) override;
  void
  doSubscribe(const envoy::extensions::filters::network::sip_proxy::v3alpha::CustomizedAffinity&
                  customized_affinity);

private:
  virtual TrafficRoutingAssistant::ClientPtr& traClient() { return tra_client_; }

  ConnectionManager& parent_;
  CacheManager<std::string, std::string, std::string> cache_manager_;
  TrafficRoutingAssistant::ClientPtr tra_client_;
  StreamInfo::StreamInfoImpl stream_info_;
  std::map<std::string, bool> is_subscribe_map_;
};

/**
 * ConnectionManager is a Network::Filter that will perform Sip request handling on a connection.
 */
class ConnectionManager : public Network::ReadFilter,
                          public Network::ConnectionCallbacks,
                          public DecoderCallbacks,
                          public PendingListHandler,
                          Logger::Loggable<Logger::Id::connection> {
public:
  ConnectionManager(Config& config, Random::RandomGenerator& random_generator,
                    TimeSource& time_system, Server::Configuration::FactoryContext& context,
                    std::shared_ptr<Router::TransactionInfos> transaction_infos, std::shared_ptr<SipFilters::DownstreamConnectionInfos> downstream_connections_info);
  ~ConnectionManager() override;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override {
    // fixme - we need a somethign better than integer thread number here 
    // - ideally some sort of base64(host@uuid) which is stored at the thread level and passed to each filter instance
    std::string thread_id = this->context_.api().threadFactory().currentThreadId().debugString();
    
    // fixme - sja3Hash or connectionID doesn't appear to be populated
    // downstream_conn_id_ = read_callbacks_->connection().connectionInfoProvider().ja3Hash().data();
    // also going to need a unique value for the connection - e.g base64(remoteIPport@host@uuid)
    Random::RandomGeneratorImpl random;
    std::string remote_address = read_callbacks_->connection().connectionInfoProvider().directRemoteAddress()->asString();
    std::string local_address = read_callbacks_->connection().connectionInfoProvider().localAddress()->asString();
    std::string uuid = random.uuid();
    std::string downstream_conn_id = remote_address + "@" + local_address + "@" + uuid;
    local_ingress_id_ = IngressID(thread_id, downstream_conn_id);

    downstream_connection_infos_->insertDownstreamConnection(downstream_conn_id, &(read_callbacks_->connection()));
    ENVOY_LOG(info, "XXXXXXXXXXXXXXXXXX thread_id={}, downstream_connection_id={}, n-connections={}", thread_id, downstream_conn_id, downstream_connection_infos_->size());
    return Network::FilterStatus::Continue; 
  }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks&) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // DecoderCallbacks
  DecoderEventHandler& newDecoderEventHandler(MessageMetadataSharedPtr metadata) override;

  std::shared_ptr<SipSettings> settings() const override { return config_.settings(); }

  void continueHandling(const std::string& key, bool try_next_affinity = false);
  void continueHandling(MessageMetadataSharedPtr metadata,
                        DecoderEventHandler& decoder_event_handler);
  std::shared_ptr<TrafficRoutingAssistantHandler> traHandler() { return this->tra_handler_; }

  // PendingListHandler
  void pushIntoPendingList(const std::string& type, const std::string& key,
                           SipFilters::DecoderFilterCallbacks& activetrans,
                           std::function<void(void)> func) override {
    return pending_list_.pushIntoPendingList(type, key, activetrans, func);
  }
  void onResponseHandleForPendingList(
      const std::string& type, const std::string& key,
      std::function<void(MessageMetadataSharedPtr metadata, DecoderEventHandler&)> func) override {
    return pending_list_.onResponseHandleForPendingList(type, key, func);
  }
  void eraseActiveTransFromPendingList(std::string& transaction_id) override {
    return pending_list_.eraseActiveTransFromPendingList(transaction_id);
  }

private:
  friend class SipConnectionManagerTest;
  struct ActiveTrans;

  struct ResponseDecoder : public DecoderCallbacks, public DecoderEventHandler {
    ResponseDecoder(ActiveTrans& parent) : parent_(parent) {}

    bool onData(MessageMetadataSharedPtr metadata);

    // DecoderEventHandler
    FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
    FilterStatus messageEnd() override;
    FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override {
      UNREFERENCED_PARAMETER(metadata);
      return FilterStatus::Continue;
    }
    FilterStatus transportEnd() override;

    // DecoderCallbacks
    DecoderEventHandler& newDecoderEventHandler(MessageMetadataSharedPtr metadata) override {
      UNREFERENCED_PARAMETER(metadata);
      return *this;
    }

    std::shared_ptr<SipSettings> settings() const override { return parent_.parent_.settings(); }

    std::shared_ptr<TrafficRoutingAssistantHandler> traHandler() {
      return parent_.parent_.tra_handler_;
    }

    ActiveTrans& parent_;
    MessageMetadataSharedPtr metadata_;
  };
  using ResponseDecoderPtr = std::unique_ptr<ResponseDecoder>;

  // Wraps a DecoderFilter and acts as the DecoderFilterCallbacks for the filter, enabling filter
  // chain continuation.
  struct ActiveTransDecoderFilter : public SipFilters::DecoderFilterCallbacks,
                                    LinkedObject<ActiveTransDecoderFilter> {
    ActiveTransDecoderFilter(ActiveTrans& parent, SipFilters::DecoderFilterSharedPtr filter)
        : parent_(parent), handle_(filter) {}

    // SipFilters::DecoderFilterCallbacks
    uint64_t streamId() const override { return parent_.streamId(); }
    std::string transactionId() const override { return parent_.transactionId(); }
    const Network::Connection* connection() const override { return parent_.connection(); }
    IngressID ingressID() override { return parent_.ingressID(); }
    Router::RouteConstSharedPtr route() override { return parent_.route(); }
    SipFilterStats& stats() override { return parent_.stats(); }
    void sendLocalReply(const DirectResponse& response, bool end_stream) override {
      parent_.sendLocalReply(response, end_stream);
    }
    void startUpstreamResponse() override { parent_.startUpstreamResponse(); }
    SipFilters::ResponseStatus upstreamData(MessageMetadataSharedPtr metadata) override {
      return parent_.upstreamData(metadata);
    }
    void resetDownstreamConnection() override { parent_.resetDownstreamConnection(); }
    StreamInfo::StreamInfo& streamInfo() override { return parent_.streamInfo(); }
    std::shared_ptr<Router::TransactionInfos> transactionInfos() override {
      return parent_.transactionInfos();
    }
    std::shared_ptr<SipFilters::DownstreamConnectionInfos> downstreamConnectionInfos() override {
      return parent_.downstreamConnectionInfos();
    }
    std::shared_ptr<SipSettings> settings() const override { return parent_.settings(); }
    std::shared_ptr<TrafficRoutingAssistantHandler> traHandler() override {
      return parent_.traHandler();
    }
    void onReset() override { return parent_.onReset(); }

    void continueHandling(const std::string& key, bool try_next_affinity) override {
      return parent_.continueHandling(key, try_next_affinity);
    }
    MessageMetadataSharedPtr metadata() override { return parent_.metadata(); }

    // PendingListHandler
    void pushIntoPendingList(const std::string& type, const std::string& key,
                             SipFilters::DecoderFilterCallbacks& activetrans,
                             std::function<void(void)> func) override {
      UNREFERENCED_PARAMETER(type);
      UNREFERENCED_PARAMETER(key);
      UNREFERENCED_PARAMETER(activetrans);
      UNREFERENCED_PARAMETER(func);
    }
    void onResponseHandleForPendingList(
        const std::string& type, const std::string& key,
        std::function<void(MessageMetadataSharedPtr, DecoderEventHandler&)> func) override {
      UNREFERENCED_PARAMETER(type);
      UNREFERENCED_PARAMETER(key);
      UNREFERENCED_PARAMETER(func);
    }
    void eraseActiveTransFromPendingList(std::string& transaction_id) override {
      UNREFERENCED_PARAMETER(transaction_id);
    }

    ActiveTrans& parent_;
    SipFilters::DecoderFilterSharedPtr handle_;
  };
  using ActiveTransDecoderFilterPtr = std::unique_ptr<ActiveTransDecoderFilter>;

  // ActiveTrans tracks request/response pairs.
  struct ActiveTrans : LinkedObject<ActiveTrans>,
                       public Event::DeferredDeletable,
                       public DecoderEventHandler,
                       public SipFilters::DecoderFilterCallbacks,
                       public SipFilters::FilterChainFactoryCallbacks {
    ActiveTrans(ConnectionManager& parent, MessageMetadataSharedPtr metadata)
        : parent_(parent), request_timer_(new Stats::HistogramCompletableTimespanImpl(
                               parent_.stats_.request_time_ms_, parent_.time_source_)),
          stream_id_(parent_.random_generator_.random()),
          transaction_id_(metadata->transactionId().value()),
          stream_info_(parent_.time_source_,
                       parent_.read_callbacks_->connection().connectionInfoProviderSharedPtr()),
          metadata_(metadata) {
      parent.stats_.request_active_.inc();
    }
    ~ActiveTrans() override {
      request_timer_->complete();
      parent_.stats_.request_active_.dec();

      parent_.eraseActiveTransFromPendingList(transaction_id_);
      for (auto& filter : decoder_filters_) {
        filter->handle_->onDestroy();
      }
    }

    // DecoderEventHandler
    FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override;
    FilterStatus transportEnd() override;
    FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
    FilterStatus messageEnd() override;

    // PendingListHandler
    void pushIntoPendingList(const std::string& type, const std::string& key,
                             SipFilters::DecoderFilterCallbacks& activetrans,
                             std::function<void(void)> func) override {
      return parent_.pushIntoPendingList(type, key, activetrans, func);
    }
    void onResponseHandleForPendingList(
        const std::string& type, const std::string& key,
        std::function<void(MessageMetadataSharedPtr metadata, DecoderEventHandler&)> func)
        override {
      return parent_.onResponseHandleForPendingList(type, key, func);
    }
    void eraseActiveTransFromPendingList(std::string& transaction_id) override {
      return parent_.eraseActiveTransFromPendingList(transaction_id);
    }

    // SipFilters::DecoderFilterCallbacks
    uint64_t streamId() const override { return stream_id_; }
    std::string transactionId() const override { return transaction_id_; }
    IngressID ingressID() override { return parent_.local_ingress_id_.value(); } // fixme - careful need to ensure theres a value
    const Network::Connection* connection() const override;
    Router::RouteConstSharedPtr route() override;
    SipFilterStats& stats() override { return parent_.stats_; }
    void sendLocalReply(const DirectResponse& response, bool end_stream) override;
    void startUpstreamResponse() override;
    SipFilters::ResponseStatus upstreamData(MessageMetadataSharedPtr metadata) override;
    void resetDownstreamConnection() override;
    StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }

    std::shared_ptr<Router::TransactionInfos> transactionInfos() override {
      return parent_.transaction_infos_;
    }
    std::shared_ptr<SipFilters::DownstreamConnectionInfos> downstreamConnectionInfos() override {
      return parent_.downstream_connection_infos_;
    }
    std::shared_ptr<SipSettings> settings() const override { return parent_.config_.settings(); }
    void onReset() override;
    std::shared_ptr<TrafficRoutingAssistantHandler> traHandler() override {
      return parent_.tra_handler_;
    }
    void continueHandling(const std::string& key, bool try_next_affinity) override {
      return parent_.continueHandling(key, try_next_affinity);
    }

    // Sip::FilterChainFactoryCallbacks
    void addDecoderFilter(SipFilters::DecoderFilterSharedPtr filter) override {
      ActiveTransDecoderFilterPtr wrapper =
          std::make_unique<ActiveTransDecoderFilter>(*this, filter);
      filter->setDecoderFilterCallbacks(wrapper->parent_);
      LinkedList::moveIntoListBack(std::move(wrapper), decoder_filters_);
    }

    FilterStatus applyDecoderFilters(ActiveTransDecoderFilter* filter);
    void finalizeRequest();

    void createFilterChain();
    void onError(const std::string& what);
    MessageMetadataSharedPtr metadata() override { return metadata_; }
    bool localResponseSent() { return local_response_sent_; }
    void setLocalResponseSent(bool local_response_sent) {
      local_response_sent_ = local_response_sent;
    }

    ConnectionManager& parent_;
    Stats::TimespanPtr request_timer_;
    uint64_t stream_id_;
    std::string transaction_id_;
    StreamInfo::StreamInfoImpl stream_info_;
    MessageMetadataSharedPtr metadata_;
    std::list<ActiveTransDecoderFilterPtr> decoder_filters_;
    ResponseDecoderPtr response_decoder_;
    absl::optional<Router::RouteConstSharedPtr> cached_route_;

    std::function<FilterStatus(DecoderEventHandler*)> filter_action_;

    absl::any filter_context_;
    bool local_response_sent_{false};

    /* Used by Router */
    std::shared_ptr<Router::TransactionInfos> transaction_infos_;
  };

  using ActiveTransPtr = std::unique_ptr<ActiveTrans>;

  void dispatch();
  void sendLocalReply(MessageMetadata& metadata, const DirectResponse& response, bool end_stream);
  void setLocalResponseSent(absl::string_view transaction_id);
  void doDeferredTransDestroy(ActiveTrans& trans);
  void resetAllTrans(bool local_reset);

  // Wrapper around a connection to enable routing of requests from upstream to downstream
  class DownstreamConnectionInfoItem: public Logger::Loggable<Logger::Id::filter>, SipFilters::DecoderFilterCallbacks {
  public:
    DownstreamConnectionInfoItem(ConnectionManager& parent)
        : parent_(parent), stream_info_(parent.time_source_, parent.read_callbacks_->connection().connectionInfoProviderSharedPtr())  {}

    ~DownstreamConnectionInfoItem() override = default;

    // // // SipFilters::DecoderFilterCallbacks
    const Network::Connection* connection() const override  { return &parent_.read_callbacks_->connection(); }

    uint64_t streamId() const override { return 0; }
    std::string transactionId() const override { return ""; }
    IngressID ingressID() override { return IngressID{"", ""}; } 

    Router::RouteConstSharedPtr route() override {
      return nullptr;
    }
    SipFilterStats& stats() override { return parent_.config_.stats(); }
    void sendLocalReply(const DirectResponse& response, bool end_stream) override{
      UNREFERENCED_PARAMETER(response);
      UNREFERENCED_PARAMETER(end_stream);
    }

    void startUpstreamResponse() override;
    SipFilters::ResponseStatus upstreamData(MessageMetadataSharedPtr metadata) override { 
      UNREFERENCED_PARAMETER(metadata);
      return SipFilters::ResponseStatus::Complete;
    };
    void resetDownstreamConnection() override { };
    StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }

    std::shared_ptr<Router::TransactionInfos> transactionInfos() override {
      return nullptr;
    }
    std::shared_ptr<SipFilters::DownstreamConnectionInfos> downstreamConnectionInfos() override {
      return nullptr;
    }
    std::shared_ptr<SipSettings> settings() const override { return parent_.config_.settings(); }
    
    void onReset() override {};

    std::shared_ptr<TrafficRoutingAssistantHandler> traHandler() override {
      return nullptr;
    }
    
    // N/A
    void continueHandling(const std::string& key, bool try_next_affinity) override {
      UNREFERENCED_PARAMETER(key);
      UNREFERENCED_PARAMETER(try_next_affinity);  
    }


  private:
    ConnectionManager& parent_;
    StreamInfo::StreamInfoImpl stream_info_;
  };

  Config& config_;
  SipFilterStats& stats_;

  Network::ReadFilterCallbacks* read_callbacks_{};

  DecoderPtr decoder_;
  absl::flat_hash_map<std::string, ActiveTransPtr> transactions_;
  Buffer::OwnedImpl request_buffer_;
  Random::RandomGenerator& random_generator_;
  TimeSource& time_source_;
  Server::Configuration::FactoryContext& context_;

  std::shared_ptr<TrafficRoutingAssistantHandler> tra_handler_;

  std::optional<IngressID> local_ingress_id_;

  // This is used in Router, put here to pass to Router
  std::shared_ptr<Router::TransactionInfos> transaction_infos_;
  std::shared_ptr<SipFilters::DownstreamConnectionInfos> downstream_connection_infos_;
  PendingList pending_list_;
};

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
