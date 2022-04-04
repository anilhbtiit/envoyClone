#include "source/extensions/common/async_files/async_file_manager_factory.h"

#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "source/common/api/os_sys_calls_impl.h"
#include "source/extensions/common/async_files/async_file_manager_thread_pool.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

SINGLETON_MANAGER_REGISTRATION(async_file_manager_factory_singleton);

class AsyncFileManagerFactoryImpl : public AsyncFileManagerFactory {
public:
  static std::shared_ptr<AsyncFileManagerFactory>
  singleton(Envoy::Singleton::Manager* singleton_manager);
  std::shared_ptr<AsyncFileManager> getAsyncFileManager(
      const envoy::extensions::common::async_files::v3::AsyncFileManagerConfig& config,
      Api::OsSysCalls* substitute_posix_file_operations = nullptr)
      ABSL_LOCKS_EXCLUDED(mu_) override;

private:
  absl::Mutex mu_;
  absl::flat_hash_map<std::string, std::shared_ptr<AsyncFileManager>>
      managers_ ABSL_GUARDED_BY(mu_);
};

std::shared_ptr<AsyncFileManagerFactory>
AsyncFileManagerFactory::singleton(Envoy::Singleton::Manager* singleton_manager) {
  return singleton_manager->getTyped<AsyncFileManagerFactory>(
      SINGLETON_MANAGER_REGISTERED_NAME(async_file_manager_factory_singleton),
      [] { return std::make_shared<AsyncFileManagerFactory>(); });
}

std::shared_ptr<AsyncFileManager> AsyncFileManagerFactoryImpl::getAsyncFileManager(
    const envoy::extensions::common::async_files::v3::AsyncFileManagerConfig& config,
    Api::OsSysCalls* substitute_posix_file_operations) {
  Api::OsSysCalls& posix = substitute_posix_file_operations == nullptr
                               ? Api::OsSysCallsSingleton::get()
                               : *substitute_posix_file_operations;
  absl::MutexLock lock(&mu_);
  auto it = managers_.find(config.id());
  if (it == managers_.end()) {
    switch (config.manager_type_case()) {
    case envoy::extensions::common::async_files::v3::AsyncFileManagerConfig::kThreadPool:
      it = managers_
               .insert({config.id(), std::make_shared<AsyncFileManagerThreadPool>(config, posix)})
               .first;
      break;
    default:
      PANIC("unrecognized AsyncFileManagerConfig::ManagerType");
    };
  }
  return it->second;
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
