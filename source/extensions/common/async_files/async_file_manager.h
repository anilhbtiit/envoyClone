#pragma once

#include <memory>

#include "source/extensions/common/async_files/async_file_action.h"
#include "source/extensions/common/async_files/async_file_handle.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

class AsyncFileManager;

// A configuration for an AsyncFileManager instance.
// To create a thread-pool-based AsyncFileManager, set thread_pool_size.
struct AsyncFileManagerConfig {
  // A thread pool size of 0 will use std::thread::hardware_concurrency() for the number of
  // threads. If unset, will try to use a different implementation.
  absl::optional<uint32_t> thread_pool_size;

  // For testing, to inject mock/fake OsSysCalls. If unset will use real file operations.
  Api::OsSysCalls* substitute_posix_file_operations = nullptr;

  // TODO(ravenblack): Put configuration options to instantiate different implementations
  // here (e.g. `io_uring`, or a Windows-compatible implementation.)

  // Create an AsyncFileManager. This must outlive all AsyncFileHandles it generates.
  // If no registered factory accepts the configuration object, this will PANIC.
  std::unique_ptr<AsyncFileManager> createManager() const;
};

// An AsyncFileManager should be a singleton or singleton-like.
// Possible subclasses currently are:
//   * AsyncFileManagerThreadPool
class AsyncFileManager {
public:
  virtual ~AsyncFileManager() = default;

  // Action to create and open a temporary file.
  //
  // The path parameter is a path to a directory in which the anonymous file will be
  // created (commonly "/tmp", for example). Even though an anonymous file is not linked and
  // has no filename, the path can be important as it determines which physical hardware the file
  // is written to (i.e. if you were to link() the file later, linking it to a path on a different
  // device is an expensive operation; or you might prefer to write temporary files to a virtual
  // filesystem or to a mounted disposable SSD.)
  // on_complete receives an AsyncFileHandle on success, or an error on failure.
  //
  // Returns a cancellation function, which aborts the operation (and closes
  // the file if opened) unless the callback has already been called.
  virtual std::function<void()>
  createAnonymousFile(absl::string_view path,
                      std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete) PURE;

  // A mode for opening existing files.
  enum class Mode { ReadOnly, WriteOnly, ReadWrite };

  // Action to asynchronously open a named file that already exists.
  // on_complete receives an AsyncFileHandle on success, or an error on failure.
  //
  // Returns a cancellation function, which aborts the operation (and closes
  // the file if opened) unless the callback has already been called.
  virtual std::function<void()>
  openExistingFile(absl::string_view filename, Mode mode,
                   std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete) PURE;

  // Action to delete a named file.
  // on_complete receives OK on success, or an error on failure.
  //
  // Returns a cancellation function, which aborts the operation
  // unless it has already been performed.
  virtual std::function<void()> unlink(absl::string_view filename,
                                       std::function<void(absl::Status)> on_complete) PURE;

  // whenReady can be used to only perform an action when the caller hits the
  // front of the thread pool's queue - this can be used to defer requesting
  // a file action until it could actually take place. For example, if you're
  // offloading data from memory to disk temporarily, if you queue the write
  // immediately then the filesystem thread owns the data until the write
  // completes, which may be blocked by heavy traffic, and it turns out you
  // want the data back before then - you can't get it back, you have to wait
  // for the write to complete and then read it back.
  //
  // If you used whenReady, you could keep the data belonging to the client
  // until it's actually the client's turn to do disk access. When whenReady's
  // callback is called, if you request the write at that time the performance
  // will be almost identical to if you had requested the write earlier, but
  // you have the opportunity to change your mind and do something different
  // in the meantime.
  //
  // The cost of using whenReady is that it requires the client to be lock
  // controlled (since the callback occurs in a different thread than the thread
  // the state belongs to), versus simpler unchained operations can use queue
  // based actions and not worry about ownership.
  std::function<void()> whenReady(std::function<void(absl::Status)> on_complete);

  // Return a string description of the configuration of the manager.
  // (This is mostly to facilitate testing.)
  virtual std::string describe() const PURE;

private:
  virtual std::function<void()> enqueue(const std::shared_ptr<AsyncFileAction> context) PURE;

  friend class AsyncFileContextBase;
  friend class AsyncFileManagerTest;
};

// Overriding this class and instantiating a static singleton registers a
// factory type. For an example see async_file_manager_thread_pool.cc
class AsyncFileManagerFactory {
public:
  AsyncFileManagerFactory();
  virtual ~AsyncFileManagerFactory() = default;

  // Returns true if the config should instantiate from this factory instance.
  virtual bool shouldUseThisFactory(const AsyncFileManagerConfig& config) const PURE;

  // Returns an instance of an AsyncFileManager based on the config.
  virtual std::unique_ptr<AsyncFileManager> create(const AsyncFileManagerConfig& config) const PURE;
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
