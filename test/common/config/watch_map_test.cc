#include <memory>

#include "envoy/common/exception.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.validate.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "common/config/watch_map.h"

#include "test/mocks/config/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::AtMost;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::NiceMock;

namespace Envoy {
namespace Config {
namespace {

// expectDeltaAndSotwUpdate() EXPECTs two birds with one function call: we want to cover both SotW
// and delta, which, while mechanically different, can behave identically for our testing purposes.
// Specifically, as a simplification for these tests, every still-present resource is updated in
// every update. Therefore, a resource can never show up in the SotW update but not the delta
// update. We can therefore use the same expected_resources for both.
void expectDeltaAndSotwUpdate(
    MockSubscriptionCallbacks& callbacks,
    const std::vector<envoy::config::endpoint::v3::ClusterLoadAssignment>& expected_resources,
    const std::vector<std::string>& expected_removals, const std::string& version) {
  EXPECT_CALL(callbacks, onConfigUpdate(_, version))
      .WillOnce(Invoke([expected_resources](const std::vector<DecodedResourceRef>& gotten_resources,
                                            const std::string&) {
        EXPECT_EQ(expected_resources.size(), gotten_resources.size());
        for (size_t i = 0; i < expected_resources.size(); i++) {
          EXPECT_TRUE(
              TestUtility::protoEqual(gotten_resources[i].get().resource(), expected_resources[i]));
        }
      }));
  EXPECT_CALL(callbacks, onConfigUpdate(_, _, _))
      .WillOnce(Invoke([expected_resources, expected_removals,
                        version](const std::vector<DecodedResourceRef>& gotten_resources,
                                 const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                                 const std::string&) {
        EXPECT_EQ(expected_resources.size(), gotten_resources.size());
        for (size_t i = 0; i < expected_resources.size(); i++) {
          EXPECT_EQ(gotten_resources[i].get().version(), version);
          EXPECT_TRUE(
              TestUtility::protoEqual(gotten_resources[i].get().resource(), expected_resources[i]));
        }
        EXPECT_EQ(expected_removals.size(), removed_resources.size());
        for (size_t i = 0; i < expected_removals.size(); i++) {
          EXPECT_EQ(expected_removals[i], removed_resources[i]);
        }
      }));
}

void expectNoUpdate(MockSubscriptionCallbacks& callbacks, const std::string& version) {
  EXPECT_CALL(callbacks, onConfigUpdate(_, version)).Times(0);
  EXPECT_CALL(callbacks, onConfigUpdate(_, _, version)).Times(0);
}

void expectEmptySotwNoDeltaUpdate(MockSubscriptionCallbacks& callbacks,
                                  const std::string& version) {
  EXPECT_CALL(callbacks, onConfigUpdate(_, version))
      .WillOnce(Invoke([](const std::vector<DecodedResourceRef>& gotten_resources,
                          const std::string&) { EXPECT_EQ(gotten_resources.size(), 0); }));
  EXPECT_CALL(callbacks, onConfigUpdate(_, _, version)).Times(0);
}

Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>
wrapInResource(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& anys,
               const std::string& version) {
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> ret;
  for (const auto& a : anys) {
    envoy::config::endpoint::v3::ClusterLoadAssignment cur_endpoint;
    a.UnpackTo(&cur_endpoint);
    auto* cur_resource = ret.Add();
    cur_resource->set_name(cur_endpoint.cluster_name());
    cur_resource->mutable_resource()->CopyFrom(a);
    cur_resource->set_version(version);
  }
  return ret;
}

// Similar to expectDeltaAndSotwUpdate(), but making the onConfigUpdate() happen, rather than
// EXPECT-ing it.
void doDeltaAndSotwUpdate(WatchMap& watch_map,
                          const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& sotw_resources,
                          const std::vector<std::string>& removed_names,
                          const std::string& version) {
  watch_map.onConfigUpdate(sotw_resources, version);

  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> delta_resources =
      wrapInResource(sotw_resources, version);
  Protobuf::RepeatedPtrField<std::string> removed_names_proto;
  for (const auto& n : removed_names) {
    *removed_names_proto.Add() = n;
  }
  watch_map.onConfigUpdate(delta_resources, removed_names_proto, version, false);
}

// Tests the simple case of a single watch. Checks that the watch will not be told of updates to
// resources it doesn't care about. Checks that the watch can later decide it does care about them,
// and then receive subsequent updates to them.
TEST(WatchMapTest, Basic) {
  MockSubscriptionCallbacks callbacks;
  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment>
      resource_decoder("cluster_name");
  WatchMap watch_map;
  Watch* watch = watch_map.addWatch(callbacks, resource_decoder);

  {
    // The watch is interested in Alice and Bob...
    std::set<std::string> update_to({"alice", "bob"});
    AddedRemoved added_removed = watch_map.updateWatchInterest(watch, update_to);
    EXPECT_EQ(update_to, added_removed.added_);
    EXPECT_TRUE(added_removed.removed_.empty());

    // ...the update is going to contain Bob and Carol...
    Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources;
    envoy::config::endpoint::v3::ClusterLoadAssignment bob;
    bob.set_cluster_name("bob");
    updated_resources.Add()->PackFrom(bob);
    envoy::config::endpoint::v3::ClusterLoadAssignment carol;
    carol.set_cluster_name("carol");
    updated_resources.Add()->PackFrom(carol);

    // ...so the watch should receive only Bob.
    std::vector<envoy::config::endpoint::v3::ClusterLoadAssignment> expected_resources;
    expected_resources.push_back(bob);

    expectDeltaAndSotwUpdate(callbacks, expected_resources, {}, "version1");
    doDeltaAndSotwUpdate(watch_map, updated_resources, {}, "version1");
  }
  {
    // The watch is now interested in Bob, Carol, Dave, Eve...
    std::set<std::string> update_to({"bob", "carol", "dave", "eve"});
    AddedRemoved added_removed = watch_map.updateWatchInterest(watch, update_to);
    EXPECT_EQ(std::set<std::string>({"carol", "dave", "eve"}), added_removed.added_);
    EXPECT_EQ(std::set<std::string>({"alice"}), added_removed.removed_);

    // ...the update is going to contain Alice, Carol, Dave...
    Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources;
    envoy::config::endpoint::v3::ClusterLoadAssignment alice;
    alice.set_cluster_name("alice");
    updated_resources.Add()->PackFrom(alice);
    envoy::config::endpoint::v3::ClusterLoadAssignment carol;
    carol.set_cluster_name("carol");
    updated_resources.Add()->PackFrom(carol);
    envoy::config::endpoint::v3::ClusterLoadAssignment dave;
    dave.set_cluster_name("dave");
    updated_resources.Add()->PackFrom(dave);

    // ...so the watch should receive only Carol and Dave.
    std::vector<envoy::config::endpoint::v3::ClusterLoadAssignment> expected_resources;
    expected_resources.push_back(carol);
    expected_resources.push_back(dave);

    expectDeltaAndSotwUpdate(callbacks, expected_resources, {"bob"}, "version2");
    doDeltaAndSotwUpdate(watch_map, updated_resources, {"bob"}, "version2");
  }
}

// Checks the following:
// First watch on a resource name ==> updateWatchInterest() returns "add it to subscription"
// Second watch on that name ==> updateWatchInterest() returns nothing about that name
// Original watch loses interest ==> nothing
// Second watch also loses interest ==> "remove it from subscription"
// NOTE: we need the resource name "dummy" to keep either watch from ever having no names watched,
// which is treated as interest in all names.
TEST(WatchMapTest, Overlap) {
  MockSubscriptionCallbacks callbacks1;
  MockSubscriptionCallbacks callbacks2;
  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment>
      resource_decoder("cluster_name");
  WatchMap watch_map;
  Watch* watch1 = watch_map.addWatch(callbacks1, resource_decoder);
  Watch* watch2 = watch_map.addWatch(callbacks2, resource_decoder);

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources;
  envoy::config::endpoint::v3::ClusterLoadAssignment alice;
  alice.set_cluster_name("alice");
  updated_resources.Add()->PackFrom(alice);

  // First watch becomes interested.
  {
    std::set<std::string> update_to({"alice", "dummy"});
    AddedRemoved added_removed = watch_map.updateWatchInterest(watch1, update_to);
    EXPECT_EQ(update_to, added_removed.added_); // add to subscription
    EXPECT_TRUE(added_removed.removed_.empty());
    watch_map.updateWatchInterest(watch2, {"dummy"});

    // *Only* first watch receives update.
    expectDeltaAndSotwUpdate(callbacks1, {alice}, {}, "version1");
    expectNoUpdate(callbacks2, "version1");
    doDeltaAndSotwUpdate(watch_map, updated_resources, {}, "version1");
  }
  // Second watch becomes interested.
  {
    std::set<std::string> update_to({"alice", "dummy"});
    AddedRemoved added_removed = watch_map.updateWatchInterest(watch2, update_to);
    EXPECT_TRUE(added_removed.added_.empty()); // nothing happens
    EXPECT_TRUE(added_removed.removed_.empty());

    // Both watches receive update.
    expectDeltaAndSotwUpdate(callbacks1, {alice}, {}, "version2");
    expectDeltaAndSotwUpdate(callbacks2, {alice}, {}, "version2");
    doDeltaAndSotwUpdate(watch_map, updated_resources, {}, "version2");
  }
  // First watch loses interest.
  {
    AddedRemoved added_removed = watch_map.updateWatchInterest(watch1, {"dummy"});
    EXPECT_TRUE(added_removed.added_.empty()); // nothing happens
    EXPECT_TRUE(added_removed.removed_.empty());

    // Both watches receive the update. For watch2, this is obviously desired.
    expectDeltaAndSotwUpdate(callbacks2, {alice}, {}, "version3");
    // For watch1, it's more subtle: the WatchMap sees that this update has no
    // resources watch1 cares about, but also knows that watch1 previously had
    // some resources. So, it must inform watch1 that it now has no resources.
    // (SotW only: delta's explicit removals avoid the need for this guessing.)
    expectEmptySotwNoDeltaUpdate(callbacks1, "version3");
    doDeltaAndSotwUpdate(watch_map, updated_resources, {}, "version3");
  }
  // Second watch loses interest.
  {
    AddedRemoved added_removed = watch_map.updateWatchInterest(watch2, {"dummy"});
    EXPECT_TRUE(added_removed.added_.empty());
    EXPECT_EQ(std::set<std::string>({"alice"}), added_removed.removed_); // remove from subscription
  }
}

// These are regression tests for #11877, validate that when two watches point at the same
// watched resource, and an update to one of the watches removes one or both of them, that
// WatchMap defers deletes and doesn't crash.
class SameWatchRemoval : public testing::Test {
public:
  void SetUp() override {
    envoy::config::endpoint::v3::ClusterLoadAssignment alice;
    alice.set_cluster_name("alice");
    updated_resources_.Add()->PackFrom(alice);
    watch1_ = watch_map_.addWatch(callbacks1_, resource_decoder_);
    watch2_ = watch_map_.addWatch(callbacks2_, resource_decoder_);
    watch_map_.updateWatchInterest(watch1_, {"alice"});
    watch_map_.updateWatchInterest(watch2_, {"alice"});
  }

  void removeAllInterest() {
    ASSERT_FALSE(watch_cb_invoked_);
    watch_cb_invoked_ = true;
    watch_map_.removeWatch(watch1_);
    watch_map_.removeWatch(watch2_);
  }

  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment>
      resource_decoder_{"cluster_name"};
  WatchMap watch_map_;
  NiceMock<MockSubscriptionCallbacks> callbacks1_;
  MockSubscriptionCallbacks callbacks2_;
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources_;
  Watch* watch1_;
  Watch* watch2_;
  bool watch_cb_invoked_{};
};

TEST_F(SameWatchRemoval, SameWatchRemovalSotw) {
  EXPECT_CALL(callbacks1_, onConfigUpdate(_, _))
      .Times(AtMost(1))
      .WillRepeatedly(InvokeWithoutArgs([this] { removeAllInterest(); }));
  EXPECT_CALL(callbacks2_, onConfigUpdate(_, _))
      .Times(AtMost(1))
      .WillRepeatedly(InvokeWithoutArgs([this] { removeAllInterest(); }));
  watch_map_.onConfigUpdate(updated_resources_, "version1");
}

TEST_F(SameWatchRemoval, SameWatchRemovalDeltaAdd) {
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> delta_resources =
      wrapInResource(updated_resources_, "version1");
  Protobuf::RepeatedPtrField<std::string> removed_names_proto;

  EXPECT_CALL(callbacks1_, onConfigUpdate(_, _, _))
      .Times(AtMost(1))
      .WillRepeatedly(InvokeWithoutArgs([this] { removeAllInterest(); }));
  EXPECT_CALL(callbacks2_, onConfigUpdate(_, _, _))
      .Times(AtMost(1))
      .WillRepeatedly(InvokeWithoutArgs([this] { removeAllInterest(); }));
  watch_map_.onConfigUpdate(delta_resources, removed_names_proto, "version1", false);
}

TEST_F(SameWatchRemoval, SameWatchRemovalDeltaRemove) {
  Protobuf::RepeatedPtrField<std::string> removed_names_proto;
  *removed_names_proto.Add() = "alice";
  EXPECT_CALL(callbacks1_, onConfigUpdate(_, _, _))
      .Times(AtMost(1))
      .WillRepeatedly(InvokeWithoutArgs([this] { removeAllInterest(); }));
  EXPECT_CALL(callbacks2_, onConfigUpdate(_, _, _))
      .Times(AtMost(1))
      .WillRepeatedly(InvokeWithoutArgs([this] { removeAllInterest(); }));
  watch_map_.onConfigUpdate({}, removed_names_proto, "version1", false);
}

// Checks the following:
// First watch on a resource name ==> updateWatchInterest() returns "add it to subscription"
// Watch loses interest ==> "remove it from subscription"
// Second watch on that name ==> "add it to subscription"
// NOTE: we need the resource name "dummy" to keep either watch from ever having no names watched,
// which is treated as interest in all names.
TEST(WatchMapTest, AddRemoveAdd) {
  MockSubscriptionCallbacks callbacks1;
  MockSubscriptionCallbacks callbacks2;
  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment>
      resource_decoder("cluster_name");
  WatchMap watch_map;
  Watch* watch1 = watch_map.addWatch(callbacks1, resource_decoder);
  Watch* watch2 = watch_map.addWatch(callbacks2, resource_decoder);

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources;
  envoy::config::endpoint::v3::ClusterLoadAssignment alice;
  alice.set_cluster_name("alice");
  updated_resources.Add()->PackFrom(alice);

  // First watch becomes interested.
  {
    std::set<std::string> update_to({"alice", "dummy"});
    AddedRemoved added_removed = watch_map.updateWatchInterest(watch1, update_to);
    EXPECT_EQ(update_to, added_removed.added_); // add to subscription
    EXPECT_TRUE(added_removed.removed_.empty());
    watch_map.updateWatchInterest(watch2, {"dummy"});

    // *Only* first watch receives update.
    expectDeltaAndSotwUpdate(callbacks1, {alice}, {}, "version1");
    expectNoUpdate(callbacks2, "version1");
    doDeltaAndSotwUpdate(watch_map, updated_resources, {}, "version1");
  }
  // First watch loses interest.
  {
    AddedRemoved added_removed = watch_map.updateWatchInterest(watch1, {"dummy"});
    EXPECT_TRUE(added_removed.added_.empty());
    EXPECT_EQ(std::set<std::string>({"alice"}),
              added_removed.removed_); // remove from subscription

    // (The xDS client should have responded to updateWatchInterest()'s return value by removing
    // Alice from the subscription, so onConfigUpdate() calls should be impossible right now.)
  }
  // Second watch becomes interested.
  {
    std::set<std::string> update_to({"alice", "dummy"});
    AddedRemoved added_removed = watch_map.updateWatchInterest(watch2, update_to);
    EXPECT_EQ(std::set<std::string>({"alice"}), added_removed.added_); // add to subscription
    EXPECT_TRUE(added_removed.removed_.empty());

    // Both watches receive the update. For watch2, this is obviously desired.
    expectDeltaAndSotwUpdate(callbacks2, {alice}, {}, "version2");
    // For watch1, it's more subtle: the WatchMap sees that this update has no
    // resources watch1 cares about, but also knows that watch1 previously had
    // some resources. So, it must inform watch1 that it now has no resources.
    // (SotW only: delta's explicit removals avoid the need for this guessing.)
    expectEmptySotwNoDeltaUpdate(callbacks1, "version2");
    doDeltaAndSotwUpdate(watch_map, updated_resources, {}, "version2");
  }
}

// Tests that nothing breaks if an update arrives that we entirely do not care about.
TEST(WatchMapTest, UninterestingUpdate) {
  MockSubscriptionCallbacks callbacks;
  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment>
      resource_decoder("cluster_name");
  WatchMap watch_map;
  Watch* watch = watch_map.addWatch(callbacks, resource_decoder);
  watch_map.updateWatchInterest(watch, {"alice"});

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> alice_update;
  envoy::config::endpoint::v3::ClusterLoadAssignment alice;
  alice.set_cluster_name("alice");
  alice_update.Add()->PackFrom(alice);

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> bob_update;
  envoy::config::endpoint::v3::ClusterLoadAssignment bob;
  bob.set_cluster_name("bob");
  bob_update.Add()->PackFrom(bob);

  // We are watching for alice, and an update for just bob arrives. It should be ignored.
  expectNoUpdate(callbacks, "version1");
  doDeltaAndSotwUpdate(watch_map, bob_update, {}, "version1");
  ::testing::Mock::VerifyAndClearExpectations(&callbacks);

  // The server sends an update adding alice and removing bob. We pay attention only to alice.
  expectDeltaAndSotwUpdate(callbacks, {alice}, {}, "version2");
  doDeltaAndSotwUpdate(watch_map, alice_update, {}, "version2");
  ::testing::Mock::VerifyAndClearExpectations(&callbacks);

  // The server sends an update removing alice and adding bob. We pay attention only to alice.
  expectDeltaAndSotwUpdate(callbacks, {}, {"alice"}, "version3");
  doDeltaAndSotwUpdate(watch_map, bob_update, {"alice"}, "version3");
  ::testing::Mock::VerifyAndClearExpectations(&callbacks);

  // Clean removal of the watch: first update to "interested in nothing", then remove.
  watch_map.updateWatchInterest(watch, {});
  watch_map.removeWatch(watch);

  // Finally, test that calling onConfigUpdate on a map with no watches doesn't break.
  doDeltaAndSotwUpdate(watch_map, bob_update, {}, "version4");
}

// Tests that a watch that specifies no particular resource interest is treated as interested in
// everything.
TEST(WatchMapTest, WatchingEverything) {
  MockSubscriptionCallbacks callbacks1;
  MockSubscriptionCallbacks callbacks2;
  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment>
      resource_decoder("cluster_name");
  WatchMap watch_map;
  /*Watch* watch1 = */ watch_map.addWatch(callbacks1, resource_decoder);
  Watch* watch2 = watch_map.addWatch(callbacks2, resource_decoder);
  // watch1 never specifies any names, and so is treated as interested in everything.
  watch_map.updateWatchInterest(watch2, {"alice"});

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources;
  envoy::config::endpoint::v3::ClusterLoadAssignment alice;
  alice.set_cluster_name("alice");
  updated_resources.Add()->PackFrom(alice);
  envoy::config::endpoint::v3::ClusterLoadAssignment bob;
  bob.set_cluster_name("bob");
  updated_resources.Add()->PackFrom(bob);

  std::vector<envoy::config::endpoint::v3::ClusterLoadAssignment> expected_resources1;
  expected_resources1.push_back(alice);
  expected_resources1.push_back(bob);
  std::vector<envoy::config::endpoint::v3::ClusterLoadAssignment> expected_resources2;
  expected_resources2.push_back(alice);

  expectDeltaAndSotwUpdate(callbacks1, expected_resources1, {}, "version1");
  expectDeltaAndSotwUpdate(callbacks2, expected_resources2, {}, "version1");
  doDeltaAndSotwUpdate(watch_map, updated_resources, {}, "version1");
}

// Delta onConfigUpdate has some slightly subtle details with how it handles the three cases where
// a watch receives {only updates, updates+removals, only removals} to its resources. This test
// exercise those cases. Also, the removal-only case tests that SotW does call a watch's
// onConfigUpdate even if none of the watch's interested resources are among the updated
// resources. (Which ensures we deliver empty config updates when a resource is dropped.)
TEST(WatchMapTest, DeltaOnConfigUpdate) {
  MockSubscriptionCallbacks callbacks1;
  MockSubscriptionCallbacks callbacks2;
  MockSubscriptionCallbacks callbacks3;
  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment>
      resource_decoder("cluster_name");
  WatchMap watch_map;
  Watch* watch1 = watch_map.addWatch(callbacks1, resource_decoder);
  Watch* watch2 = watch_map.addWatch(callbacks2, resource_decoder);
  Watch* watch3 = watch_map.addWatch(callbacks3, resource_decoder);
  watch_map.updateWatchInterest(watch1, {"updated"});
  watch_map.updateWatchInterest(watch2, {"updated", "removed"});
  watch_map.updateWatchInterest(watch3, {"removed"});

  // First, create the "removed" resource. We want to test SotW being handed an empty
  // onConfigUpdate. But, if SotW holds no resources, then an update with nothing it cares about
  // will just not trigger any onConfigUpdate at all.
  {
    Protobuf::RepeatedPtrField<ProtobufWkt::Any> prepare_removed;
    envoy::config::endpoint::v3::ClusterLoadAssignment will_be_removed_later;
    will_be_removed_later.set_cluster_name("removed");
    prepare_removed.Add()->PackFrom(will_be_removed_later);
    expectDeltaAndSotwUpdate(callbacks2, {will_be_removed_later}, {}, "version0");
    expectDeltaAndSotwUpdate(callbacks3, {will_be_removed_later}, {}, "version0");
    doDeltaAndSotwUpdate(watch_map, prepare_removed, {}, "version0");
  }

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> update;
  envoy::config::endpoint::v3::ClusterLoadAssignment updated;
  updated.set_cluster_name("updated");
  update.Add()->PackFrom(updated);

  expectDeltaAndSotwUpdate(callbacks1, {updated}, {}, "version1");          // only update
  expectDeltaAndSotwUpdate(callbacks2, {updated}, {"removed"}, "version1"); // update+remove
  expectDeltaAndSotwUpdate(callbacks3, {}, {"removed"}, "version1");        // only remove
  doDeltaAndSotwUpdate(watch_map, update, {"removed"}, "version1");
}

TEST(WatchMapTest, OnConfigUpdateFailed) {
  WatchMap watch_map;
  // calling on empty map doesn't break
  watch_map.onConfigUpdateFailed(ConfigUpdateFailureReason::UpdateRejected, nullptr);

  MockSubscriptionCallbacks callbacks1;
  MockSubscriptionCallbacks callbacks2;
  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment>
      resource_decoder("cluster_name");
  watch_map.addWatch(callbacks1, resource_decoder);
  watch_map.addWatch(callbacks2, resource_decoder);

  EXPECT_CALL(callbacks1, onConfigUpdateFailed(ConfigUpdateFailureReason::UpdateRejected, nullptr));
  EXPECT_CALL(callbacks2, onConfigUpdateFailed(ConfigUpdateFailureReason::UpdateRejected, nullptr));
  watch_map.onConfigUpdateFailed(ConfigUpdateFailureReason::UpdateRejected, nullptr);
}

// verifies that a watch for an alias is removed, while the watch for the prefix is kept
TEST(WatchMapTest, RemoveAliasWatches) {
  MockSubscriptionCallbacks callbacks;
  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment>
      resource_decoder("cluster_name");
  WatchMap watch_map;
  Watch* watch = watch_map.addWatch(callbacks, resource_decoder);
  watch_map.updateWatchInterest(watch, {"prefix", "prefix/alias"});

  envoy::service::discovery::v3::Resource resource;
  resource.set_name("prefix/resource");
  resource.set_version("version");
  for (const auto alias : {"prefix/alias", "prefix/alias1", "prefix/alias2"}) {
    resource.add_aliases(alias);
  }

  AddedRemoved converted = watch_map.removeAliasWatches(resource);

  EXPECT_EQ(std::set<std::string>{"prefix/alias"}, converted.removed_);
}

// verifies that a watch for an alias is removed, while the watch for the prefix is kept, even
// if the alias is the same as the resource name
TEST(WatchMapTest, RemoveAliasWatchesAliasIsSameAsName) {
  MockSubscriptionCallbacks callbacks;
  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment>
      resource_decoder("cluster_name");
  WatchMap watch_map;
  Watch* watch = watch_map.addWatch(callbacks, resource_decoder);
  watch_map.updateWatchInterest(watch, {"prefix", "prefix/name-and-alias"});

  envoy::service::discovery::v3::Resource resource;
  resource.set_name("prefix/name-and-alias");
  resource.set_version("version");
  for (const auto alias : {"prefix/name-and-alias", "prefix/alias1", "prefix/alias2"}) {
    resource.add_aliases(alias);
  }

  AddedRemoved converted = watch_map.removeAliasWatches(resource);

  EXPECT_TRUE(converted.added_.empty());
  EXPECT_EQ(std::set<std::string>{"prefix/name-and-alias"}, converted.removed_);
}

} // namespace
} // namespace Config
} // namespace Envoy
