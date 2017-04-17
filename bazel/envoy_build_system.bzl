load("@protobuf_bzl//:protobuf.bzl", "cc_proto_library")

ENVOY_COPTS = [
    # TODO(htuch): Remove this when Bazel bringup is done.
    "-DBAZEL_BRINGUP",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wnon-virtual-dtor",
    "-Woverloaded-virtual",
    "-Wold-style-cast",
    "-std=c++0x",
    "-includeprecompiled/precompiled.h",
] + select({
    # Bazel adds an implicit -DNDEBUG for opt.
    "//bazel:opt_build": [],
    "//bazel:fastbuild_build": [],
    "//bazel:dbg_build": ["-ggdb3"],
}) + select({
    "//bazel:disable_tcmalloc": [],
    "//conditions:default": ["-DTCMALLOC"],
}) + select({
    # Allow debug symbols to be added to opt/fastbuild as well.
    "//bazel:debug_symbols": ["-ggdb3"],
    "//conditions:default": [],
})

# References to Envoy external dependencies should be wrapped with this function.
def envoy_external_dep_path(dep):
    return "//external:%s" % dep

# Dependencies on tcmalloc_and_profiler should be wrapped with this function.
def tcmalloc_external_dep():
    return select({
        "//bazel:disable_tcmalloc": None,
        "//conditions:default": envoy_external_dep_path("tcmalloc_and_profiler"),
    })

# As above, but wrapped in list form for adding to dep lists. This smell seems needed as
# SelectorValue values have to match the attribute type. See
# https://github.com/bazelbuild/bazel/issues/2273.
def tcmalloc_external_deps():
    return select({
        "//bazel:disable_tcmalloc": [],
        "//conditions:default": [envoy_external_dep_path("tcmalloc_and_profiler")],
    })

# Transform the package path (e.g. include/envoy/common) into a path for
# exporting the package headers at (e.g. envoy/common). Source files can then
# include using this path scheme (e.g. #include "envoy/common/time.h").
def envoy_include_prefix(path):
    if path.startswith('source/') or path.startswith('include/'):
        return '/'.join(path.split('/')[1:])
    return None

# Envoy C++ library targets should be specified with this function.
def envoy_cc_library(name,
                     srcs = [],
                     hdrs = [],
                     copts = [],
                     visibility = None,
                     external_deps = [],
                     tcmalloc_dep = None,
                     repository = "",
                     deps = []):
    if tcmalloc_dep:
        deps += tcmalloc_external_deps()
    native.cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        copts = ENVOY_COPTS + copts,
        visibility = visibility,
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps] + [
            repository + "//include/envoy/common:base_includes",
            repository + "//source/precompiled:precompiled_includes",
        ],
        include_prefix = envoy_include_prefix(PACKAGE_NAME),
        alwayslink = 1,
    )

# Envoy C++ binary targets should be specified with this function.
def envoy_cc_binary(name,
                    srcs = [],
                    data = [],
                    visibility = None,
                    repository = "",
                    deps = []):
    native.cc_binary(
        name = name,
        srcs = srcs,
        data = data,
        copts = ENVOY_COPTS,
        linkopts = [
            "-pthread",
            "-lrt",
            "-static-libstdc++",
            "-static-libgcc",
        ],
        linkstatic = 1,
        visibility = visibility,
        malloc = tcmalloc_external_dep(),
        deps = deps + [
            repository + "//source/precompiled:precompiled_includes",
        ],
    )

# Envoy C++ test targets should be specified with this function.
def envoy_cc_test(name,
                  srcs = [],
                  data = [],
                  # List of pairs (Bazel shell script target, shell script args)
                  repository = "",
                  deps = [],
                  tags = [],
                  coverage = True,
                  local = False):
    test_lib_tags = []
    if coverage:
      test_lib_tags.append("coverage_test_lib")
    envoy_cc_test_library(
        name = name + "_lib",
        srcs = srcs,
        data = data,
        deps = deps,
        repository = repository,
        tags = test_lib_tags,
    )
    native.cc_test(
        name = name,
        copts = ENVOY_COPTS,
        linkopts = ["-pthread"],
        linkstatic = 1,
        malloc = tcmalloc_external_dep(),
        deps = [
            ":" + name + "_lib",
            repository + "//test:main"
        ],
        tags = tags + ["coverage_test"],
        local = local,
    )

# Envoy C++ test related libraries (that want gtest, gmock) should be specified
# with this function.
def envoy_cc_test_library(name,
                          srcs = [],
                          hdrs = [],
                          data = [],
                          external_deps = [],
                          deps = [],
                          repository = "",
                          tags = []):
    native.cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        data = data,
        copts = ENVOY_COPTS + ["-includetest/precompiled/precompiled_test.h"],
        testonly = 1,
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps] + [
            repository + "//source/precompiled:precompiled_includes",
            repository + "//test/precompiled:precompiled_includes",
        ],
        tags = tags,
        alwayslink = 1,
    )

# Envoy C++ mock targets should be specified with this function.
def envoy_cc_mock(name, **kargs):
    envoy_cc_test_library(name = name, **kargs)

# Envoy shell tests that need to be included in coverage run should be specified with this function.
def envoy_sh_test(name,
                  srcs = [],
                  data = [],
                  **kargs):
  test_runner_cc = name + "_test_runner.cc"
  native.genrule(
      name = name + "_gen_test_runner",
      srcs = srcs,
      outs = [test_runner_cc],
      cmd = "$(location //bazel:gen_sh_test_runner.sh) $(location " + srcs[0] + ") >> $@",
      tools = ["//bazel:gen_sh_test_runner.sh"],
  )
  envoy_cc_test_library(
      name = name + "_lib",
      srcs = [test_runner_cc],
      data = srcs + data,
      tags = ["coverage_test_lib"],
      deps = ["//test/test_common:environment_lib"],
  )
  native.sh_test(
      name = name,
      srcs = srcs,
      data = srcs + data,
      **kargs
  )

def _proto_header(proto_path):
  if proto_path.endswith(".proto"):
      return proto_path[:-5] + "pb.h"
  return None

# Envoy proto targets should be specified with this function.
def envoy_proto_library(name, srcs = [], deps = []):
    internal_name = name + "_internal"
    cc_proto_library(
        name = internal_name,
        srcs = srcs,
        default_runtime = "//external:protobuf",
        protoc = "//external:protoc",
        deps = deps,
    )
    # We can't use include_prefix directly in cc_proto_library, since it
    # confuses protoc. Instead, we create a shim cc_library that performs the
    # remap of .pb.h location to Envoy canonical header paths.
    native.cc_library(
        name = name,
        hdrs = [_proto_header(s) for s in srcs if _proto_header(s)],
        include_prefix = envoy_include_prefix(PACKAGE_NAME),
        deps = [internal_name],
    )
