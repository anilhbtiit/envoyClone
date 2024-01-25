#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Api {
namespace External {

/**
 * Register an external runtime API for usage (e.g. in extensions).
 */
void registerApi(absl::string_view name, void* api);

/**
 * Retrieve an external runtime API for usage (e.g. in extensions).
 */
void* retrieveApi(absl::string_view name, bool allow_absent = false);

} // namespace External
} // namespace Api
} // namespace Envoy
