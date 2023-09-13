#include <memory>

#include "source/common/common/inline_map.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

TEST(InlineMapWithZeroInlineKey, InlineMapWithZeroInlineKeyTest) {
  InlineMapDescriptor<std::string> descriptor;
  descriptor.finalize();

  InlineMap<std::string, std::string> map(descriptor);

  // Set entries.
  for (size_t i = 0; i < 100; ++i) {
    map.set(absl::StrCat("key_", i), absl::StrCat("value_", i));
  }

  // Set entries by duplicate keys will fail.
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_FALSE(map.set(absl::StrCat("key_", i), absl::StrCat("value_", i)).second);
  }

  // Use operator[] to set entries could overwrite existing entries.
  for (size_t i = 0; i < 100; ++i) {
    map[absl::StrCat("key_", i)] = absl::StrCat("value_", i, "_new");
  }

  // Get entries.
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_EQ(*map.get(absl::StrCat("key_", i)), absl::StrCat("value_", i, "_new"));
  }

  // Get entries by non-existing keys.
  EXPECT_FALSE(map.get("non_existing_key"));

  // Erase entries.
  for (size_t i = 0; i < 100; ++i) {
    map.erase(absl::StrCat("key_", i));
  }
}

TEST(InlineMapWith20InlineKey, InlineMapWith20InlineKeyTest) {
  InlineMapDescriptor<std::string> descriptor;

  std::vector<InlineMapDescriptor<std::string>::Handle> handles;

  // Create 20 inline keys.
  for (size_t i = 0; i < 20; ++i) {
    handles.push_back(descriptor.addInlineKey(absl::StrCat("key_", i)));
  }

  // Add repeated inline keys will make no effect and return the same handle.
  for (size_t i = 0; i < 20; ++i) {
    EXPECT_EQ(handles[i], descriptor.addInlineKey(absl::StrCat("key_", i)));
  }

  descriptor.finalize();

  InlineMap<std::string, std::string> map(descriptor);

  // Set entries by normal keys. But these keys are registered as inline keys.
  for (size_t i = 0; i < 10; ++i) {
    EXPECT_TRUE(map.set(absl::StrCat("key_", i), absl::StrCat("value_", i)).second);
    EXPECT_EQ(map.size(), i + 1);
  }

  // Set entries by duplicate keys will fail.
  for (size_t i = 0; i < 10; ++i) {
    EXPECT_FALSE(map.set(absl::StrCat("key_", i), absl::StrCat("value_", i)).second);
    EXPECT_EQ(map.size(), 10);
  }

  // Set entries by typed inline handle.
  for (size_t i = 10; i < 20; ++i) {
    auto handle = handles[i];
    EXPECT_TRUE(map.set(handle, absl::StrCat("value_", i)).second);
    EXPECT_EQ(map.size(), i + 1);
  }

  // Set entries by duplicate keys will fail.
  for (size_t i = 0; i < 20; ++i) {
    auto handle = handles[i];
    EXPECT_FALSE(map.set(handle, absl::StrCat("value_", i)).second);

    // The size will not change.
    EXPECT_EQ(map.size(), 20);
  }

  // Set entries by normal keys.
  for (size_t i = 20; i < 100; ++i) {
    EXPECT_TRUE(map.set(absl::StrCat("key_", i), absl::StrCat("value_", i)).second);
    EXPECT_EQ(map.size(), i + 1);
  }

  // Set entries by duplicate keys will fail.
  for (size_t i = 20; i < 100; ++i) {
    EXPECT_FALSE(map.set(absl::StrCat("key_", i), absl::StrCat("value_", i)).second);
    EXPECT_EQ(map.size(), 100);
  }

  // Use operator[] to set entries with typed inline handle could overwrite existing keys.
  for (size_t i = 0; i < 10; ++i) {
    map[handles[i]] = absl::StrCat("value_", i, "_new");

    // The size will not change.
    EXPECT_EQ(map.size(), 100);
  }

  // Use operator[] to set entries could overwrite existing keys (10-20 will be the keys that
  // registered as inline keys).
  for (size_t i = 10; i < 100; ++i) {
    map[absl::StrCat("key_", i)] = absl::StrCat("value_", i, "_new");

    // The size will not change.
    EXPECT_EQ(map.size(), 100);
  }

  // Get entries.
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_EQ(*map.get(absl::StrCat("key_", i)), absl::StrCat("value_", i, "_new"));

    // The size will not change.
    EXPECT_EQ(map.size(), 100);
  }

  // Get entries by typed inline handle.
  for (size_t i = 0; i < handles.size(); ++i) {
    const std::string key = absl::StrCat("key_", i);
    auto handle = handles[i];
    EXPECT_EQ(*map.get(handle), absl::StrCat("value_", i, "_new"));

    // The size will not change.
    EXPECT_EQ(map.size(), 100);
  }

  // Get entries by non-existing keys.
  EXPECT_FALSE(map.get("non_existing_key"));

  // Erase entries by typed inline handle.
  for (size_t i = 0; i < 10; ++i) {
    const std::string key = absl::StrCat("key_", i);
    auto handle = handles[i];
    map.erase(handle);

    EXPECT_EQ(map.size(), 100 - i - 1);
  }

  // Erase entries.
  for (size_t i = 10; i < 100; ++i) {
    map.erase(absl::StrCat("key_", i));

    EXPECT_EQ(map.size(), 100 - i - 1);
  }

  EXPECT_EQ(map.size(), 0);

  // Get entries from empty map by normal key will return nothing.
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_EQ(map.get(absl::StrCat("key_", i)), OptRef<std::string>{});
  }

  // Get entries from empty map by typed inline handle will return nothing.
  for (auto handle : handles) {
    EXPECT_EQ(map.get(handle), OptRef<std::string>{});
  }

  // Operator[] will insert new entry if the key does not exist.
  for (size_t i = 0; i < 10; ++i) {
    EXPECT_EQ(map[handles[i]], "");

    EXPECT_EQ(map.size(), i + 1);
  }

  for (size_t i = 10; i < 100; ++i) {
    EXPECT_EQ(map[absl::StrCat("key_", i)], "");

    EXPECT_EQ(map.size(), i + 1);
  }

  EXPECT_EQ(map.size(), 100);
}

TEST(InlineMapWith20InlineKey, InlineMapWith20InlineKeyTestDestructWithEntries) {
  InlineMapDescriptor<std::string> descriptor;

  std::vector<InlineMapDescriptor<std::string>::Handle> handles;

  // Create 20 inline keys.
  for (size_t i = 0; i < 20; ++i) {
    handles.push_back(descriptor.addInlineKey(absl::StrCat("key_", i)));
  }

  // Add repeated inline keys will make no effect and return the same handle.
  for (size_t i = 0; i < 20; ++i) {
    EXPECT_EQ(handles[i], descriptor.addInlineKey(absl::StrCat("key_", i)));
  }

  descriptor.finalize();

  {
    InlineMap<std::string, std::string> map(descriptor);

    // Set inline entries.
    for (size_t i = 0; i < 20; ++i) {
      map.set(handles[i], absl::StrCat("value_", i));
    }

    // Set dynamic entries.
    for (size_t i = 20; i < 100; ++i) {
      map.set(absl::StrCat("key_", i), absl::StrCat("value_", i));
    }
  }
}

TEST(InlineMapWith20InlineKey, InlineMapWith20InlineKeyTestWithUniquePtrAsValue) {
  InlineMapDescriptor<std::string> descriptor;

  std::vector<InlineMapDescriptor<std::string>::Handle> handles;

  // Create 20 inline keys.
  for (size_t i = 0; i < 20; ++i) {
    handles.push_back(descriptor.addInlineKey(absl::StrCat("key_", i)));
  }

  // Add repeated inline keys will make no effect and return the same handle.
  for (size_t i = 0; i < 20; ++i) {
    EXPECT_EQ(handles[i], descriptor.addInlineKey(absl::StrCat("key_", i)));
  }

  descriptor.finalize();

  InlineMap<std::string, std::unique_ptr<std::string>> map(descriptor);

  // Set inline entries.
  for (size_t i = 0; i < 20; ++i) {
    map.set(handles[i], std::make_unique<std::string>(absl::StrCat("value_", i)));
  }

  // Set dynamic entries.
  for (size_t i = 20; i < 100; ++i) {
    map.set(absl::StrCat("key_", i), std::make_unique<std::string>(absl::StrCat("value_", i)));
  }

  // Erase entries by typed inline handle.
  for (size_t i = 0; i < 5; ++i) {
    map.erase(handles[i]);
  }

  // Overwrite entries by typed inline handle.
  for (size_t i = 5; i < 10; ++i) {
    map[handles[i]] = std::make_unique<std::string>(absl::StrCat("value_", i, "_new"));
  }

  // Erase entries by dynamic key.
  for (size_t i = 20; i < 25; ++i) {
    map.erase(absl::StrCat("key_", i));
  }

  // Overwrite entries by dynamic key.
  for (size_t i = 25; i < 30; ++i) {
    map[absl::StrCat("key_", i)] = std::make_unique<std::string>(absl::StrCat("value_", i, "_new"));
  }

  // Clear the map.
  map.clear();

  EXPECT_EQ(map.size(), 0);

  // Reset the entries.
  for (size_t i = 0; i < 100; ++i) {
    map.set(absl::StrCat("key_", i), std::make_unique<std::string>(absl::StrCat("value_", i)));
  }

  EXPECT_EQ(map.size(), 100);
}

TEST(InlineMapWith3InlineKey, TestInlineKeysAsString) {
  InlineMapDescriptor<std::string> descriptor;
  // Create 3 inline keys.
  for (size_t i = 0; i < 3; ++i) {
    descriptor.addInlineKey(absl::StrCat("key_", i));
  }

  descriptor.finalize();

  EXPECT_EQ(descriptor.inlineKeysAsString(), "key_0,key_1,key_2");
  EXPECT_EQ(descriptor.inlineKeysAsString(", "), "key_0, key_1, key_2");
  EXPECT_EQ(descriptor.inlineKeysAsString(" | "), "key_0 | key_1 | key_2");
  EXPECT_EQ(descriptor.inlineKeysAsString("-"), "key_0-key_1-key_2");
}

TEST(InlineMapWith3InlineKey, TestInlineMapMoveConstructor) {
  InlineMapDescriptor<std::string> descriptor;
  // Create 3 inline keys.
  for (size_t i = 0; i < 3; ++i) {
    descriptor.addInlineKey(absl::StrCat("key_", i));
  }

  descriptor.finalize();

  InlineMap<std::string, std::unique_ptr<std::string>> map(descriptor);
  for (size_t i = 0; i < 10; ++i) {
    map.set(absl::StrCat("key_", i), std::make_unique<std::string>(absl::StrCat("value_", i)));
  }

  // Check values by the keys.
  for (size_t i = 0; i < 10; ++i) {
    EXPECT_EQ(*map.get(absl::StrCat("key_", i)).ref(), absl::StrCat("value_", i));
  }

  InlineMap<std::string, std::unique_ptr<std::string>> map2(std::move(map));

  // Check original map is empty.
  EXPECT_EQ(map.size(), 0);
  EXPECT_EQ(map2.size(), 10);

  // Check values by the keys.
  for (size_t i = 0; i < 10; ++i) {
    EXPECT_EQ(*map2.get(absl::StrCat("key_", i)).ref(), absl::StrCat("value_", i));
  }
}

} // namespace
} // namespace Envoy
