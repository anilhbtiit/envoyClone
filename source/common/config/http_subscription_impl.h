#pragma once

#include <memory>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/config/subscription.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/common/macros.h"
#include "common/config/utility.h"
#include "common/http/headers.h"
#include "common/http/rest_api_fetcher.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "google/api/annotations.pb.h"

namespace Envoy {
namespace Config {

/**
 * REST implementation of the API Subscription interface. This fetches the API via periodic polling
 * with jitter (based on RestApiFetcher). The REST requests are POSTs of the JSON canonical
 * representation of the DiscoveryRequest proto and the responses are in the form of the JSON
 * canonical representation of DiscoveryResponse. This implementation is responsible for translating
 * between the proto serializable objects in the Subscription API and the REST JSON representation.
 */
class HttpSubscriptionImpl : public Http::RestApiFetcher,
                             public Config::Subscription,
                             Logger::Loggable<Logger::Id::config> {
public:
  HttpSubscriptionImpl(const LocalInfo::LocalInfo& local_info, Upstream::ClusterManager& cm,
                       const std::string& remote_cluster_name, Event::Dispatcher& dispatcher,
                       Runtime::RandomGenerator& random, std::chrono::milliseconds refresh_interval,
                       std::chrono::milliseconds request_timeout,
                       const Protobuf::MethodDescriptor& service_method, SubscriptionStats stats,
                       std::chrono::milliseconds init_fetch_timeout)
      : Http::RestApiFetcher(cm, remote_cluster_name, dispatcher, random, refresh_interval,
                             request_timeout),
        stats_(stats), dispatcher_(dispatcher), init_fetch_timeout_(init_fetch_timeout) {
    request_.mutable_node()->CopyFrom(local_info.node());
    ASSERT(service_method.options().HasExtension(google::api::http));
    const auto& http_rule = service_method.options().GetExtension(google::api::http);
    path_ = http_rule.post();
    ASSERT(http_rule.body() == "*");
  }

  // Config::Subscription
  void start(const std::set<std::string>& resources,
             Config::SubscriptionCallbacks& callbacks) override {
    ASSERT(callbacks_ == nullptr);

    if (init_fetch_timeout_.count() > 0) {
      init_fetch_timeout_timer_ = dispatcher_.createTimer([this]() -> void {
        ENVOY_LOG(warn, "REST config: initial fetch timed out for", path_);
        callbacks_->onConfigUpdateFailed(nullptr);
      });
      init_fetch_timeout_timer_->enableTimer(init_fetch_timeout_);
    }

    Protobuf::RepeatedPtrField<ProtobufTypes::String> resources_vector(resources.begin(),
                                                                       resources.end());
    request_.mutable_resource_names()->Swap(&resources_vector);
    callbacks_ = &callbacks;
    initialize();
  }

  void updateResources(const std::set<std::string>& update_to_these_names) override {
    Protobuf::RepeatedPtrField<ProtobufTypes::String> resources_vector(
        update_to_these_names.begin(), update_to_these_names.end());
    request_.mutable_resource_names()->Swap(&resources_vector);
  }

  // Http::RestApiFetcher
  void createRequest(Http::Message& request) override {
    ENVOY_LOG(debug, "Sending REST request for {}", path_);
    stats_.update_attempt_.inc();
    request.headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Post);
    request.headers().insertPath().value(path_);
    request.body() =
        std::make_unique<Buffer::OwnedImpl>(MessageUtil::getJsonStringFromMessage(request_));
    request.headers().insertContentType().value().setReference(
        Http::Headers::get().ContentTypeValues.Json);
    request.headers().insertContentLength().value(request.body()->length());
  }

  void parseResponse(const Http::Message& response) override {
    disableInitFetchTimeoutTimer();
    envoy::api::v2::DiscoveryResponse message;
    try {
      MessageUtil::loadFromJson(response.bodyAsString(), message);
    } catch (const EnvoyException& e) {
      ENVOY_LOG(warn, "REST config JSON conversion error: {}", e.what());
      handleFailure(nullptr);
      return;
    }
    try {
      callbacks_->onConfigUpdate(message.resources(), message.version_info());
      request_.set_version_info(message.version_info());
      stats_.version_.set(HashUtil::xxHash64(request_.version_info()));
      stats_.update_success_.inc();
    } catch (const EnvoyException& e) {
      ENVOY_LOG(warn, "REST config update rejected: {}", e.what());
      stats_.update_rejected_.inc();
      callbacks_->onConfigUpdateFailed(&e);
    }
  }

  void onFetchComplete() override {}

  void onFetchFailure(const EnvoyException* e) override {
    disableInitFetchTimeoutTimer();
    ENVOY_LOG(warn, "REST config update failed: {}", e != nullptr ? e->what() : "fetch failure");
    handleFailure(e);
  }

private:
  void handleFailure(const EnvoyException* e) {
    stats_.update_failure_.inc();
    callbacks_->onConfigUpdateFailed(e);
  }

  void disableInitFetchTimeoutTimer() {
    if (init_fetch_timeout_timer_) {
      init_fetch_timeout_timer_->disableTimer();
      init_fetch_timeout_timer_.reset();
    }
  }

  std::string path_;
  Protobuf::RepeatedPtrField<ProtobufTypes::String> resources_;
  Config::SubscriptionCallbacks* callbacks_{};
  envoy::api::v2::DiscoveryRequest request_;
  SubscriptionStats stats_;
  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds init_fetch_timeout_;
  Event::TimerPtr init_fetch_timeout_timer_;
};

} // namespace Config
} // namespace Envoy
