#include "test/test_common/global.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Test {

Globals& Globals::instance() {
  static Globals* h = new Globals;
  return *h;
}

std::string Globals::describeActiveSingletonsHelper() {
  std::string ret;
  Thread::ReleasableLockGuard map_lock(map_mutex_);
  for (auto& p : singleton_map_) {
    std::unique_ptr<Singleton>& singleton = p.second;
    ASSERT(singleton != nullptr);
    if (singleton->ptr_ != nullptr) {
      absl::StrAppend(&ret, "Unexpected active singleton: ", p.first, "\n");
    }
  }
  return ret;
}

Globals::Singleton& Globals::get(const std::string& type_name, const MakeObjectFn& make_object) {
  Thread::ReleasableLockGuard map_lock(map_mutex_);
  std::unique_ptr<Singleton>& singleton = singleton_map_[type_name];

  if (singleton == nullptr) {
    // The first time constructing this object, we'll be constructing the
    // Singleton for the first time, populating it with a fresh instance,
    // so no need to take the singleton's mutex. But we do need to hold
    // the map mutex as we are installing the singleton object in the
    // singleton_map.
    singleton = std::make_unique<Singleton>(make_object(), map_mutex_);
    return *singleton;
  }

  if (singleton->ptr_ == nullptr) {
    ASSERT(singleton->ref_count_ == 0);
    singleton->ptr_ = make_object();
  }
  ++singleton->ref_count_;
  return *singleton;
}

void Globals::Singleton::releaseHelper(const DeleteObjectFn& delete_object) {
  void* obj_to_delete = nullptr;
  {
    Thread::LockGuard singleton_lock(mutex_);
    ASSERT(ptr_ != nullptr);
    if (--ref_count_ == 0) {
      obj_to_delete = ptr_;
      ptr_ = nullptr;
    }
  }
  if (obj_to_delete != nullptr) {
    delete_object(obj_to_delete);
  }
}

} // namespace Test
} // namespace Envoy
