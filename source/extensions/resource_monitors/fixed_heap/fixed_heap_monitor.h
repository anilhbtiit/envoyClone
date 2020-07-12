#pragma once

#include <memory>

#include "envoy/config/resource_monitor/fixed_heap/v2alpha/fixed_heap.pb.h"
#include "envoy/server/resource_monitor.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {

/**
 * Helper class for getting memory heap stats.
 */
class MemoryStatsReader {
public:
  MemoryStatsReader() = default;
  virtual ~MemoryStatsReader() = default;

  // Memory reserved for the process by the heap.
  virtual uint64_t reservedHeapBytes();
  // Memory in free, unmapped pages in the page heap.
  virtual uint64_t unmappedHeapBytes();
};

using MemoryStatsReaderPtr = std::unique_ptr<MemoryStatsReader>;

/**
 * Heap memory monitor with a statically configured maximum.
 */
class FixedHeapMonitor : public Server::ResourceMonitor {
public:
  FixedHeapMonitor(
      const envoy::config::resource_monitor::fixed_heap::v2alpha::FixedHeapConfig& config,
      MemoryStatsReaderPtr stats = std::make_unique<MemoryStatsReader>());

  void updateResourceUsage(Server::ResourceMonitor::Callbacks& callbacks) override;

private:
  const uint64_t max_heap_;
  MemoryStatsReaderPtr stats_;
};

using FixedHeapMonitorPtr = std::unique_ptr<FixedHeapMonitor>;

} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
