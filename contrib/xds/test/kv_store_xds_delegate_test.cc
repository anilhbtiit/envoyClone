#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"
#include "envoy/service/runtime/v3/rtds.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.validate.h"

#include "source/common/config/xds_source_id.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "contrib/xds/source/kv_store_xds_delegate.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

using ::Envoy::Config::DecodedResourceRef;
using ::Envoy::Config::XdsConfigSourceId;
using ::Envoy::Config::XdsSourceId;

envoy::config::core::v3::TypedExtensionConfig kvStoreDelegateConfig() {
  const std::string filename = TestEnvironment::temporaryPath("xds_kv_store.txt");
  ::unlink(filename.c_str());

  const std::string config_str = fmt::format(R"EOF(
    name: envoy.config.xds.KeyValueStoreXdsDelegate
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.xds.v3.KeyValueStoreXdsDelegateConfig
      key_value_store_config:
        config:
          name: envoy.key_value.file_based
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.key_value.file_based.v3.FileBasedKeyValueStoreConfig
            filename: {}
    )EOF",
                                             filename);

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(config_str, config);
  return config;
}

class KeyValueStoreXdsDelegateTest : public testing::Test {
public:
  KeyValueStoreXdsDelegateTest() {
    auto config = kvStoreDelegateConfig();
    Extensions::Config::KeyValueStoreXdsDelegateFactory delegate_factory;
    xds_delegate_ = delegate_factory.createXdsResourcesDelegate(
        config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), api_, dispatcher_);
  }

protected:
  envoy::service::runtime::v3::Runtime parseYamlIntoRuntimeResource(const std::string& yaml) {
    envoy::service::runtime::v3::Runtime runtime;
    TestUtility::loadFromYaml(yaml, runtime);
    return runtime;
  }

  envoy::config::cluster::v3::Cluster parseYamlIntoClusterResource(const std::string& yaml) {
    envoy::config::cluster::v3::Cluster cluster;
    TestUtility::loadFromYaml(yaml, cluster);
    return cluster;
  }

  template <typename Resource>
  void checkSavedResources(const XdsSourceId& source_id,
                           const std::vector<DecodedResourceRef>& expected_resources) {
    // Retrieve the xDS resources.
    const auto retrieved_resources = xds_delegate_->getResources(source_id, /*resource_names=*/{});
    // Check that they're the same.
    EXPECT_EQ(expected_resources.size(), retrieved_resources.size());
    for (size_t i = 0; i < expected_resources.size(); ++i) {
      Resource unpacked_resource;
      MessageUtil::unpackTo(retrieved_resources[i].resource(), unpacked_resource);
      TestUtility::protoEqual(expected_resources[i].get().resource(), unpacked_resource);
    }
  }

  testing::NiceMock<Api::MockApi> api_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  Config::XdsResourcesDelegatePtr xds_delegate_;
};

TEST_F(KeyValueStoreXdsDelegateTest, SaveAndRetrieve) {
  const std::string authority_1 = "rtds_cluster";
  auto runtime_resource_1 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_1
    layer:
      foo: bar
      baz: meh
  )EOF");
  auto runtime_resource_2 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_2
    layer:
      abc: xyz
  )EOF");
  const auto saved_resources =
      TestUtility::decodeResources({runtime_resource_1, runtime_resource_2});
  const XdsConfigSourceId source_id{authority_1, Config::TypeUrl::get().Runtime};
  // Save xDS resources.
  xds_delegate_->onConfigUpdated(source_id, saved_resources.refvec_);

  checkSavedResources<envoy::service::runtime::v3::Runtime>(source_id, saved_resources.refvec_);
}

TEST_F(KeyValueStoreXdsDelegateTest, MultipleAuthoritiesAndTypes) {
  const std::string authority_1 = "rtds_cluster";
  const std::string authority_2 = "127.0.0.1:8585";

  auto runtime_resource_1 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_1
    layer:
      foo: bar
      baz: meh
  )EOF");
  auto runtime_resource_2 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_2
    layer:
      abc: xyz
  )EOF");
  auto cluster_resource_1 = parseYamlIntoClusterResource(R"EOF(
    name: cluster_1
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF");

  const auto authority_1_runtime_resources = TestUtility::decodeResources({runtime_resource_1});
  const auto authority_2_runtime_resources = TestUtility::decodeResources({runtime_resource_2});
  const auto authority_2_cluster_resources = TestUtility::decodeResources({cluster_resource_1});

  const XdsConfigSourceId source_id_1{authority_1, Config::TypeUrl::get().Runtime};
  const XdsConfigSourceId source_id_2_runtime{authority_2, Config::TypeUrl::get().Runtime};
  const XdsConfigSourceId source_id_2_cluster{authority_2, Config::TypeUrl::get().Cluster};

  // Save xDS resources.
  xds_delegate_->onConfigUpdated(source_id_1, authority_1_runtime_resources.refvec_);
  xds_delegate_->onConfigUpdated(source_id_2_runtime, authority_2_runtime_resources.refvec_);
  xds_delegate_->onConfigUpdated(source_id_2_cluster, authority_2_cluster_resources.refvec_);

  checkSavedResources<envoy::service::runtime::v3::Runtime>(source_id_1,
                                                            authority_1_runtime_resources.refvec_);
  checkSavedResources<envoy::service::runtime::v3::Runtime>(source_id_2_runtime,
                                                            authority_2_runtime_resources.refvec_);
  checkSavedResources<envoy::config::cluster::v3::Cluster>(source_id_2_cluster,
                                                           authority_2_cluster_resources.refvec_);
}

TEST_F(KeyValueStoreXdsDelegateTest, UpdatedSotwResources) {
  const std::string authority_1 = "rtds_cluster";
  auto runtime_resource_1 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_1
    layer:
      foo: bar
      baz: meh
  )EOF");
  auto runtime_resource_2 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_2
    layer:
      abc: xyz
  )EOF");

  const XdsConfigSourceId source_id{authority_1, Config::TypeUrl::get().Runtime};

  // Save xDS resources.
  const auto saved_resources =
      TestUtility::decodeResources({runtime_resource_1, runtime_resource_2});
  xds_delegate_->onConfigUpdated(source_id, saved_resources.refvec_);

  // Update xDS resources.
  runtime_resource_2 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_2
    layer:
      abc: klm
  )EOF");
  auto runtime_resource_3 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_3
    layer:
      xyz: 123
  )EOF");
  const auto updated_saved_resources = TestUtility::decodeResources({runtime_resource_3});
  xds_delegate_->onConfigUpdated(source_id, updated_saved_resources.refvec_);

  // Make sure all resources are present and at their latest versions.
  const auto all_resources =
      TestUtility::decodeResources({runtime_resource_1, runtime_resource_2, runtime_resource_3});
  checkSavedResources<envoy::service::runtime::v3::Runtime>(source_id, all_resources.refvec_);
}
// TODO(abeyad): add test for resource eviction.

} // namespace
} // namespace Envoy
