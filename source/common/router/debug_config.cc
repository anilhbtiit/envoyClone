#include "source/common/router/debug_config.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Router {

auto debug_config_inline_key =
    InlineMapRegistryHelper::registerInlinKey<StreamInfo::FilterStateInlineMapScope>(
        "envoy.router.debug_config");

DebugConfig::DebugConfig(bool append_cluster, absl::optional<Http::LowerCaseString> cluster_header,
                         bool append_upstream_host,
                         absl::optional<Http::LowerCaseString> hostname_header,
                         absl::optional<Http::LowerCaseString> host_address_header,
                         bool do_not_forward,
                         absl::optional<Http::LowerCaseString> not_forwarded_header)
    : append_cluster_(append_cluster), cluster_header_(std::move(cluster_header)),
      append_upstream_host_(append_upstream_host), hostname_header_(std::move(hostname_header)),
      host_address_header_(std::move(host_address_header)), do_not_forward_(do_not_forward),
      not_forwarded_header_(std::move(not_forwarded_header)) {}

const StreamInfo::InlineKey DebugConfig::key() { return debug_config_inline_key; }

} // namespace Router
} // namespace Envoy
