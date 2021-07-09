#pragma once

#include <cstdint>
#include <memory>
#include <queue>

#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/common/token_bucket.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/status.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/config/api_version.h"
#include "source/common/config/grpc_stream.h"
#include "source/common/config/pausable_ack_queue.h"
#include "source/common/config/watch_map.h"
#include "source/common/config/xds_mux/delta_subscription_state.h"
#include "source/common/config/xds_mux/sotw_subscription_state.h"
#include "source/common/grpc/common.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Config {
namespace XdsMux {

// Manages subscriptions to one or more type of resource. The logical protocol
// state of those subscription(s) is handled by SubscriptionState.
// This class owns the GrpcStream used to talk to the server, maintains queuing
// logic to properly order the subscription(s)' various messages, and allows
// starting/stopping/pausing of the subscriptions.
template <class S, class F, class RQ, class RS>
class GrpcMuxImpl : public GrpcMux, Logger::Loggable<Logger::Id::config> {
public:
  GrpcMuxImpl(std::unique_ptr<F> subscription_state_factory, bool skip_subsequent_node,
              const LocalInfo::LocalInfo& local_info,
              envoy::config::core::v3::ApiVersion transport_api_version);

  Watch* addWatch(const std::string& type_url, const absl::flat_hash_set<std::string>& resources,
                  SubscriptionCallbacks& callbacks, OpaqueResourceDecoder& resource_decoder,
                  const bool use_namespace_matching = false) override;
  void updateWatch(const std::string& type_url, Watch* watch,
                   const absl::flat_hash_set<std::string>& resources,
                   const bool creating_namespace_watch = false) override;
  void removeWatch(const std::string& type_url, Watch* watch) override;

  ScopedResume pause(const std::string& type_url) override;
  ScopedResume pause(const std::vector<std::string> type_urls) override;
  bool paused(const std::string& type_url) const override;
  void start() override;
  const absl::flat_hash_map<std::string, std::unique_ptr<S>>& subscriptions() const {
    return subscriptions_;
  }

  // legacy mux interface not implemented by unified mux.
  GrpcMuxWatchPtr addWatch(const std::string&, const absl::flat_hash_set<std::string>&,
                           SubscriptionCallbacks&, OpaqueResourceDecoder&,
                           const SubscriptionOptions&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  void requestOnDemandUpdate(const std::string&, const absl::flat_hash_set<std::string>&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  bool isUnified() const override { return true; }

protected:
  // Everything related to GrpcStream must remain abstract. GrpcStream (and the gRPC-using classes
  // that underlie it) are templated on protobufs. That means that a single implementation that
  // supports different types of protobufs cannot use polymorphism to share code. The workaround:
  // the GrpcStream will be owned by a derived class, and all code that would touch grpc_stream_ is
  // seen here in the base class as calls to abstract functions, to be provided by those derived
  // classes.
  virtual void establishGrpcStream() PURE;
  // Deletes msg_proto_ptr.
  virtual void sendGrpcMessage(RQ& msg_proto, S& sub_state) PURE;
  virtual void maybeUpdateQueueSizeStat(uint64_t size) PURE;
  virtual bool grpcStreamAvailable() const PURE;
  virtual bool rateLimitAllowsDrain() PURE;

  S& subscriptionStateFor(const std::string& type_url);
  WatchMap& watchMapFor(const std::string& type_url);
  void handleEstablishedStream();
  void handleStreamEstablishmentFailure();
  void genericHandleResponse(const std::string& type_url, const RS& response_proto);
  void trySendDiscoveryRequests();
  bool skipSubsequentNode() const { return skip_subsequent_node_; }
  bool anyRequestSentYetInCurrentStream() const { return any_request_sent_yet_in_current_stream_; }
  void setAnyRequestSentYetInCurrentStream(bool value) {
    any_request_sent_yet_in_current_stream_ = value;
  }
  const LocalInfo::LocalInfo& localInfo() const { return local_info_; }
  const envoy::config::core::v3::ApiVersion& transportApiVersion() const {
    return transport_api_version_;
  }

private:
  // Checks whether external conditions allow sending a DeltaDiscoveryRequest. (Does not check
  // whether we *want* to send a DeltaDiscoveryRequest).
  bool canSendDiscoveryRequest(const std::string& type_url);

  // Checks whether we have something to say in a DeltaDiscoveryRequest, which can be an ACK and/or
  // a subscription update. (Does not check whether we *can* send that DeltaDiscoveryRequest).
  // Returns the type_url we should send the DeltaDiscoveryRequest for (if any).
  // First, prioritizes ACKs over non-ACK subscription interest updates.
  // Then, prioritizes non-ACK updates in the order the various types
  // of subscriptions were activated (as tracked by subscription_ordering_).
  absl::optional<std::string> whoWantsToSendDiscoveryRequest();

  // Invoked when dynamic context parameters change for a resource type.
  void onDynamicContextUpdate(absl::string_view resource_type_url);

  // Resource (N)ACKs we're waiting to send, stored in the order that they should be sent in. All
  // of our different resource types' ACKs are mixed together in this queue. See class for
  // description of how it interacts with pause() and resume().
  PausableAckQueue pausable_ack_queue_;

  // Makes SubscriptionStates, to be held in the subscriptions_ map. Whether this GrpcMux is doing
  // delta or state of the world xDS is determined by which concrete subclass this variable gets.
  std::unique_ptr<F> subscription_state_factory_;

  // Map key is type_url.
  // Only addWatch() should insert into these maps.
  absl::flat_hash_map<std::string, std::unique_ptr<S>> subscriptions_;
  absl::flat_hash_map<std::string, std::unique_ptr<WatchMap>> watch_maps_;

  // Determines the order of initial discovery requests. (Assumes that subscriptions are added
  // to this GrpcMux in the order of Envoy's dependency ordering).
  std::list<std::string> subscription_ordering_;

  // Whether to enable the optimization of only including the node field in the very first
  // discovery request in an xDS gRPC stream (really just one: *not* per-type_url).
  const bool skip_subsequent_node_;

  // State to help with skip_subsequent_node's logic.
  bool any_request_sent_yet_in_current_stream_{};

  // Used to populate the [Delta]DiscoveryRequest's node field. That field is the same across
  // all type_urls, and moreover, the 'skip_subsequent_node' logic needs to operate across all
  // the type_urls. So, while the SubscriptionStates populate every other field of these messages,
  // this one is up to GrpcMux.
  const LocalInfo::LocalInfo& local_info_;
  Common::CallbackHandlePtr dynamic_update_callback_handle_;

  const envoy::config::core::v3::ApiVersion transport_api_version_;
};

class GrpcMuxDelta
    : public GrpcMuxImpl<DeltaSubscriptionState, DeltaSubscriptionStateFactory,
                         envoy::service::discovery::v3::DeltaDiscoveryRequest,
                         envoy::service::discovery::v3::DeltaDiscoveryResponse>,
      public GrpcStreamCallbacks<envoy::service::discovery::v3::DeltaDiscoveryResponse> {
public:
  GrpcMuxDelta(Grpc::RawAsyncClientPtr&& async_client, Event::Dispatcher& dispatcher,
               const Protobuf::MethodDescriptor& service_method,
               envoy::config::core::v3::ApiVersion transport_api_version,
               Random::RandomGenerator& random, Stats::Scope& scope,
               const RateLimitSettings& rate_limit_settings, const LocalInfo::LocalInfo& local_info,
               bool skip_subsequent_node);

  // GrpcStreamCallbacks
  void onStreamEstablished() override;
  void onEstablishmentFailure() override;
  void onWriteable() override;
  void onDiscoveryResponse(
      std::unique_ptr<envoy::service::discovery::v3::DeltaDiscoveryResponse>&& message,
      ControlPlaneStats& control_plane_stats) override;
  void requestOnDemandUpdate(const std::string& type_url,
                             const absl::flat_hash_set<std::string>& for_update) override;

protected:
  void establishGrpcStream() override;
  void sendGrpcMessage(envoy::service::discovery::v3::DeltaDiscoveryRequest& msg_proto,
                       DeltaSubscriptionState& sub_state) override;
  void maybeUpdateQueueSizeStat(uint64_t size) override;
  bool grpcStreamAvailable() const override;
  bool rateLimitAllowsDrain() override;

private:
  GrpcStream<envoy::service::discovery::v3::DeltaDiscoveryRequest,
             envoy::service::discovery::v3::DeltaDiscoveryResponse>
      grpc_stream_;
};

class GrpcMuxSotw : public GrpcMuxImpl<SotwSubscriptionState, SotwSubscriptionStateFactory,
                                       envoy::service::discovery::v3::DiscoveryRequest,
                                       envoy::service::discovery::v3::DiscoveryResponse>,
                    public GrpcStreamCallbacks<envoy::service::discovery::v3::DiscoveryResponse> {
public:
  GrpcMuxSotw(Grpc::RawAsyncClientPtr&& async_client, Event::Dispatcher& dispatcher,
              const Protobuf::MethodDescriptor& service_method,
              envoy::config::core::v3::ApiVersion transport_api_version,
              Random::RandomGenerator& random, Stats::Scope& scope,
              const RateLimitSettings& rate_limit_settings, const LocalInfo::LocalInfo& local_info,
              bool skip_subsequent_node);

  // GrpcStreamCallbacks
  void onStreamEstablished() override;
  void onEstablishmentFailure() override;
  void onWriteable() override;
  void
  onDiscoveryResponse(std::unique_ptr<envoy::service::discovery::v3::DiscoveryResponse>&& message,
                      ControlPlaneStats& control_plane_stats) override;
  void requestOnDemandUpdate(const std::string&, const absl::flat_hash_set<std::string>&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  };
  GrpcStream<envoy::service::discovery::v3::DiscoveryRequest,
             envoy::service::discovery::v3::DiscoveryResponse>&
  grpcStreamForTest() {
    return grpc_stream_;
  }

protected:
  void establishGrpcStream() override;
  void sendGrpcMessage(envoy::service::discovery::v3::DiscoveryRequest& msg_proto,
                       SotwSubscriptionState& sub_state) override;
  void maybeUpdateQueueSizeStat(uint64_t size) override;
  bool grpcStreamAvailable() const override;
  bool rateLimitAllowsDrain() override;

private:
  GrpcStream<envoy::service::discovery::v3::DiscoveryRequest,
             envoy::service::discovery::v3::DiscoveryResponse>
      grpc_stream_;
};

class NullGrpcMuxImpl : public GrpcMux {
public:
  void start() override {}

  ScopedResume pause(const std::string&) override {
    return std::make_unique<Cleanup>([]() {});
  }
  ScopedResume pause(const std::vector<std::string>) override {
    return std::make_unique<Cleanup>([]() {});
  }
  bool paused(const std::string&) const override { return false; }
  void disableInitFetchTimeoutTimer() override {}

  Watch* addWatch(const std::string&, const absl::flat_hash_set<std::string>&,
                  SubscriptionCallbacks&, OpaqueResourceDecoder&, const bool) override;
  void updateWatch(const std::string&, Watch*, const absl::flat_hash_set<std::string>&,
                   const bool) override;
  void removeWatch(const std::string&, Watch*) override;

  // legacy mux interface not implemented by unified mux.
  GrpcMuxWatchPtr addWatch(const std::string&, const absl::flat_hash_set<std::string>&,
                           SubscriptionCallbacks&, OpaqueResourceDecoder&,
                           const SubscriptionOptions&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  void requestOnDemandUpdate(const std::string&, const absl::flat_hash_set<std::string>&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
};

} // namespace XdsMux
} // namespace Config
} // namespace Envoy
