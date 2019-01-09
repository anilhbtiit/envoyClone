# Transformations to QUICHE tarball:
# - Move subtree under quiche/ base dir, for clarity in #include statements.
# - Rewrite include directives for platform/impl files.
#
# The mechanics of this are likely to change as QUICHE evolves, supplies its own
# Bazel buildfiles, and perhaps provides a more graceful way to override
# platform impl directory location. However, the end result (QUICHE files placed
# under quiche/{http2,quic,spdy}/, with the Envoy-specific implementation of the
# QUICHE platform APIs in //source/extensions/quic_listeners/quiche/platform/,
# should remain the same.

src_files = glob(["**/*.h", "**/*.c", "**/*.cc", "**/*.inc", "**/*.proto"])

genrule(
    name = "quiche_files",
    srcs = src_files,
    outs = ["quiche/" + f for f in src_files],
    cmd = "\n".join(
        ["sed -e '/^#include/ s!net/\(http2\|quic\|spdy\)/platform/impl/!extensions/quic_listeners/quiche/platform/!' $(location %s) > $(location :%s)" % (f, "quiche/" + f) for f in src_files],
    ),
    visibility = ["//visibility:private"],
)

cc_library(
    name = "http2_platform",
    hdrs = [
        "quiche/http2/platform/api/http2_arraysize.h",
        "quiche/http2/platform/api/http2_bug_tracker.h",
        "quiche/http2/platform/api/http2_containers.h",
        "quiche/http2/platform/api/http2_estimate_memory_usage.h",
        "quiche/http2/platform/api/http2_export.h",
        "quiche/http2/platform/api/http2_flags.h",
        "quiche/http2/platform/api/http2_flag_utils.h",
        "quiche/http2/platform/api/http2_macros.h",
        "quiche/http2/platform/api/http2_mock_log.h",
        "quiche/http2/platform/api/http2_optional.h",
        "quiche/http2/platform/api/http2_ptr_util.h",
        "quiche/http2/platform/api/http2_reconstruct_object.h",
        "quiche/http2/platform/api/http2_string.h",
        "quiche/http2/platform/api/http2_string_piece.h",
        "quiche/http2/platform/api/http2_string_utils.h",
        "quiche/http2/platform/api/http2_test_helpers.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@envoy//source/extensions/quic_listeners/quiche/platform:http2_platform_impl_lib",
    ],
)
