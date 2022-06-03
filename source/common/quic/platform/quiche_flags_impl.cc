// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <set>

#include "source/common/common/assert.h"
#include "source/common/http/utility.h"

#include "absl/flags/flag.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "quiche_platform_impl/quiche_flags_impl.h"

// Flag definitions
#define QUIC_FLAG(flag, value) ABSL_FLAG(bool, flag, value, "");
#include "quiche/quic/core/quic_flags_list.h"
QUIC_FLAG(quic_reloadable_flag_spdy_testonly_default_false, false)
QUIC_FLAG(quic_reloadable_flag_spdy_testonly_default_true, true)
QUIC_FLAG(quic_restart_flag_spdy_testonly_default_false, false)
QUIC_FLAG(quic_restart_flag_spdy_testonly_default_true, true)
QUIC_FLAG(quic_reloadable_flag_http2_testonly_default_false, false)
QUIC_FLAG(quic_reloadable_flag_http2_testonly_default_true, true)
QUIC_FLAG(quic_restart_flag_http2_testonly_default_false, false)
QUIC_FLAG(quic_restart_flag_http2_testonly_default_true, true)
#undef QUIC_FLAG

#define QUIC_PROTOCOL_FLAG(type, flag, value, help) ABSL_FLAG(type, flag, value, help);
#include "quiche/quic/core/quic_protocol_flags_list.h"
#undef QUIC_PROTOCOL_FLAG

namespace quiche {

namespace {

absl::flat_hash_map<absl::string_view, ReloadableFlag*> makeReloadableFlagMap() {
  absl::flat_hash_map<absl::string_view, ReloadableFlag*> flags;

  ASSERT(absl::GetFlag(FLAGS_quic_restart_flag_quic_testonly_default_true) == true);
#define QUIC_FLAG(flag, ...) flags.emplace("FLAGS_"#flag, &FLAGS_##flag);
#include "quiche/quic/core/quic_flags_list.h"
  QUIC_FLAG(quic_reloadable_flag_spdy_testonly_default_false, false)
  QUIC_FLAG(quic_reloadable_flag_spdy_testonly_default_true, true)
  QUIC_FLAG(quic_restart_flag_spdy_testonly_default_false, false)
  QUIC_FLAG(quic_restart_flag_spdy_testonly_default_true, true)
  QUIC_FLAG(quic_reloadable_flag_http2_testonly_default_false, false)
  QUIC_FLAG(quic_reloadable_flag_http2_testonly_default_true, true)
  QUIC_FLAG(quic_restart_flag_http2_testonly_default_false, false)
  QUIC_FLAG(quic_restart_flag_http2_testonly_default_true, true)
#undef QUIC_FLAG
  return flags;
}

} // namespace

FlagRegistry::FlagRegistry() : reloadable_flags_(makeReloadableFlagMap()) {
  // Envoy only supports RFC-v1 in the long term, so disable IETF draft 29 implementation by
  // default.
  absl::SetFlag(&FLAGS_quic_reloadable_flag_quic_disable_version_draft_29, true);
  // This flag enables BBR, otherwise QUIC will use Cubic which is less performant.
  absl::SetFlag(&FLAGS_quic_reloadable_flag_quic_default_to_bbr, true);

  // Do not include 32-byte per-entry overhead while counting header size.
  absl::SetFlag(&FLAGS_quic_header_size_limit_includes_overhead, false);

  // Set send buffer twice of max flow control window to ensure that stream send
  // buffer always takes all the data.
  // The max amount of data buffered is the per-stream high watermark + the max
  // flow control window of upstream. The per-stream high watermark should be
  // smaller than max flow control window to make sure upper stream can be flow
  // control blocked early enough not to send more than the threshold allows.
  // TODO(#8826) Ideally we should use the negotiated value from upstream which is not accessible
  // for now. 512MB is way to large, but the actual bytes buffered should be bound by the negotiated
  // upstream flow control window.
  absl::SetFlag(&FLAGS_quic_buffered_data_threshold,
      2 * ::Envoy::Http2::Utility::OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE); // 512MB
}

// static
FlagRegistry& FlagRegistry::getInstance() {
  static auto* instance = new FlagRegistry();
  return *instance;
}

void FlagRegistry::updateReloadableFlags(
    const absl::flat_hash_map<std::string, bool>& quiche_flags_override) {
  for (auto& [flag_name, flag] : reloadable_flags_) {
    const auto it = quiche_flags_override.find(flag_name);
    if (it != quiche_flags_override.end()) {
      absl::SetFlag(flag, it->second);
    }
  }
}

} // namespace quiche
