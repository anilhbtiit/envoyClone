#include "envoy/registry/registry.h"

#include "extensions/common/wasm/wasm_runtime_factory.h"

#include "include/proxy-wasm/null.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class NullRuntimeFactory : public WasmRuntimeFactory {
public:
  WasmVmPtr createWasmVm() override { return proxy_wasm::createNullVm(); }

  absl::string_view name() override { return "envoy.wasm.runtime.null"; }
  absl::string_view short_name() override { return "null"; }
};

REGISTER_FACTORY(NullRuntimeFactory, WasmRuntimeFactory);

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
