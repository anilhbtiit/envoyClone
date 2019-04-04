#pragma once

#include "envoy/api/v2/eds.pb.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/config/xds_context.h"

#include "common/config/resources.h"
#include "common/protobuf/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Config {

template <class ResourceType> class MockSubscriptionCallbacks : public SubscriptionCallbacks {
public:
  MockSubscriptionCallbacks() {
    ON_CALL(*this, resourceName(testing::_))
        .WillByDefault(testing::Invoke([](const ProtobufWkt::Any& resource) -> std::string {
          return resourceName_(MessageUtil::anyConvert<ResourceType>(resource));
        }));
  }
  ~MockSubscriptionCallbacks() override {}
  static std::string resourceName_(const envoy::api::v2::ClusterLoadAssignment& resource) {
    return resource.cluster_name();
  }
  template <class T> static std::string resourceName_(const T& resource) { return resource.name(); }

  // TODO(fredlas) deduplicate
  MOCK_METHOD2_T(onConfigUpdate, void(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                      const std::string& version_info));
  MOCK_METHOD3_T(onConfigUpdate,
                 void(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string& system_version_info));
  MOCK_METHOD1_T(onConfigUpdateFailed, void(const EnvoyException* e));
  MOCK_METHOD1_T(resourceName, std::string(const ProtobufWkt::Any& resource));
};

class MockSubscription : public Subscription {
public:
  MOCK_METHOD2_T(start,
                 void(const std::vector<std::string>& resources, SubscriptionCallbacks& callbacks));
  MOCK_METHOD1_T(updateResources, void(const std::vector<std::string>& resources));
};

class MockGrpcMuxWatch : public GrpcMuxWatch {
public:
  MockGrpcMuxWatch();
  virtual ~MockGrpcMuxWatch();

  MOCK_METHOD0(cancel, void());
};

class MockGrpcMux : public GrpcMux {
public:
  MockGrpcMux();
  virtual ~MockGrpcMux();

  MOCK_METHOD0(start, void());
  MOCK_METHOD3(subscribe_,
               GrpcMuxWatch*(const std::string& type_url, const std::vector<std::string>& resources,
                             GrpcMuxCallbacks& callbacks));
  GrpcMuxWatchPtr subscribe(const std::string& type_url, const std::vector<std::string>& resources,
                            GrpcMuxCallbacks& callbacks);
  MOCK_METHOD1(pause, void(const std::string& type_url));
  MOCK_METHOD1(resume, void(const std::string& type_url));
};

class MockXdsGrpcContext : public XdsGrpcContext {
public:
  MockXdsGrpcContext();
  virtual ~MockXdsGrpcContext();

  MOCK_METHOD5(addSubscription,
               void(const std::vector<std::string>& resources, const std::string& type_url,
                    SubscriptionCallbacks& callbacks, SubscriptionStats& stats,
                    std::chrono::milliseconds init_fetch_timeout));
  MOCK_METHOD2(updateResources,
               void(const std::vector<std::string>& resources, const std::string& type_url));

  MOCK_METHOD1(removeSubscription, void(const std::string& type_url));
  MOCK_METHOD1(pause, void(const std::string& type_url));
  MOCK_METHOD1(resume, void(const std::string& type_url));

  MOCK_METHOD0(drainRequests, void());
  MOCK_METHOD0(handleStreamEstablished, void());
  MOCK_METHOD0(handleEstablishmentFailure, void());

  // GrpcMux TODO TODO remove
  MOCK_METHOD0(start, void());
  MOCK_METHOD3(subscribe_,
               GrpcMuxWatch*(const std::string& type_url, const std::vector<std::string>& resources,
                             GrpcMuxCallbacks& callbacks));
  GrpcMuxWatchPtr subscribe(const std::string& type_url, const std::vector<std::string>& resources,
                            GrpcMuxCallbacks& callbacks);
};

class MockGrpcMuxCallbacks : public GrpcMuxCallbacks {
public:
  MockGrpcMuxCallbacks();
  virtual ~MockGrpcMuxCallbacks();

  MOCK_METHOD2(onConfigUpdate, void(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                    const std::string& version_info));
  MOCK_METHOD1(onConfigUpdateFailed, void(const EnvoyException* e));
  MOCK_METHOD1(resourceName, std::string(const ProtobufWkt::Any& resource));
};

} // namespace Config
} // namespace Envoy
