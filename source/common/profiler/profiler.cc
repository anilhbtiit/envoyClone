#include "profiler.h"

#ifdef TCMALLOC

#include "gperftools/heap-profiler.h"
#include "gperftools/profiler.h"

namespace Profiler {

bool Cpu::profilerEnabled() { return ProfilingIsEnabledForAllThreads(); }

void Cpu::startProfiler(const std::string& output_path) { ProfilerStart(output_path.c_str()); }

void Cpu::stopProfiler() { ProfilerStop(); }

void Heap::forceLink() {
  // Currently this is here to force the inclusion of the heap profiler during static linking.
  // Without this call the heap profiler will not be included and cannot be started via env
  // variable. In the future we can add admin support.
  HeapProfilerDump("");
}

} // Profiler

#else

namespace Profiler {

bool Cpu::profilerEnabled() { return false; }
void Cpu::startProfiler(const std::string&) {}
void Cpu::stopProfiler() {}

} // Profiler

#endif // #ifdef TCMALLOC
