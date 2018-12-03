#pragma once

#include "envoy/api/api.h"
#include "envoy/common/pure.h"
#include "envoy/server/watchdog.h"

namespace Envoy {
namespace Server {

/**
 * The GuardDog runs a background thread which scans a number of shared WatchDog
 * objects periodically to verify that they have been recently touched. If some
 * of the watched items have not responded the GuardDog will take action ranging
 * from stats counter increments to killing the entire process (if a deadlock is
 * suspected).
 *
 * The lifespan of the GuardDog thread is tied to the lifespan of this object.
 */
class GuardDog {
public:
  virtual ~GuardDog() {}

  /**
   * Get a WatchDog object pointer to a new WatchDog.
   *
   * After this method returns the WatchDog object must be touched periodically
   * to avoid triggering the GuardDog. If no longer needed use the
   * stopWatching() method to remove it from the list of watched objects.
   *
   * @param api A reference to an Api::Api object the WatchDog uses to get its system thread ID
   */
  virtual WatchDogSharedPtr createWatchDog(Api::Api& api) PURE;

  /**
   * Tell the GuardDog to forget about this WatchDog.
   * After calling this method it is no longer necessary to touch the WatchDog
   * object.
   *
   * @param wd A WatchDogSharedPtr obtained from createWatchDog.
   */
  virtual void stopWatching(WatchDogSharedPtr wd) PURE;
};

} // namespace Server
} // namespace Envoy
