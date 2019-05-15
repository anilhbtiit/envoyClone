load("@rules_foreign_cc//tools/build_defs:cmake.bzl", "cmake_external")
load(":envoy_binary.bzl", _envoy_cc_binary = "envoy_cc_binary")
load(":envoy_internal.bzl", "envoy_external_dep_path")
load(
    ":envoy_library.bzl",
    _envoy_basic_cc_library = "envoy_basic_cc_library",
    _envoy_cc_library = "envoy_cc_library",
    _envoy_cc_posix_library = "envoy_cc_posix_library",
    _envoy_cc_win32_library = "envoy_cc_win32_library",
    _envoy_include_prefix = "envoy_include_prefix",
    _envoy_proto_library = "envoy_proto_library",
)
load(
    ":envoy_select.bzl",
    _envoy_select_google_grpc = "envoy_select_google_grpc",
    _envoy_select_hot_restart = "envoy_select_hot_restart",
)
load(
    ":envoy_test.bzl",
    _envoy_cc_fuzz_test = "envoy_cc_fuzz_test",
    _envoy_cc_mock = "envoy_cc_mock",
    _envoy_cc_test = "envoy_cc_test",
    _envoy_cc_test_binary = "envoy_cc_test_binary",
    _envoy_cc_test_library = "envoy_cc_test_library",
    _envoy_py_test_binary = "envoy_py_test_binary",
    _envoy_sh_test = "envoy_sh_test",
)

def envoy_package():
    native.package(default_visibility = ["//visibility:public"])

# A genrule variant that can output a directory. This is useful when doing things like
# generating a fuzz corpus mechanically.
def _envoy_directory_genrule_impl(ctx):
    tree = ctx.actions.declare_directory(ctx.attr.name + ".outputs")
    ctx.actions.run_shell(
        inputs = ctx.files.srcs,
        tools = ctx.files.tools,
        outputs = [tree],
        command = "mkdir -p " + tree.path + " && " + ctx.expand_location(ctx.attr.cmd),
        env = {"GENRULE_OUTPUT_DIR": tree.path},
    )
    return [DefaultInfo(files = depset([tree]))]

envoy_directory_genrule = rule(
    implementation = _envoy_directory_genrule_impl,
    attrs = {
        "srcs": attr.label_list(),
        "cmd": attr.string(),
        "tools": attr.label_list(),
    },
)

def _filter_windows_keys(cache_entries = {}):
    # On Windows, we don't want to explicitly set CMAKE_BUILD_TYPE,
    # rules_foreign_cc will figure it out for us
    return {key: cache_entries[key] for key in cache_entries.keys() if key != "CMAKE_BUILD_TYPE"}

# External CMake C++ library targets should be specified with this function. This defaults
# to building the dependencies with ninja
def envoy_cmake_external(
        name,
        cache_entries = {},
        debug_cache_entries = {},
        cmake_options = ["-GNinja"],
        make_commands = ["ninja", "ninja install"],
        lib_source = "",
        postfix_script = "",
        static_libraries = [],
        copy_pdb = False,
        pdb_name = "",
        cmake_files_dir = "$BUILD_TMPDIR/CMakeFiles",
        **kwargs):
    cache_entries_debug = dict(cache_entries)
    cache_entries_debug.update(debug_cache_entries)

    pf = ""
    if copy_pdb:
        if pdb_name == "":
            pdb_name = name

        copy_command = "cp {cmake_files_dir}/{pdb_name}.dir/{pdb_name}.pdb $INSTALLDIR/lib/{pdb_name}.pdb".format(cmake_files_dir = cmake_files_dir, pdb_name = pdb_name)
        if postfix_script != "":
            copy_command = copy_command + " && " + postfix_script

        pf = select({
            "@envoy//bazel:windows_dbg_build": copy_command,
            "//conditions:default": postfix_script,
        })
    else:
        pf = postfix_script

    cmake_external(
        name = name,
        cache_entries = select({
            "@envoy//bazel:windows_opt_build": _filter_windows_keys(cache_entries),
            "@envoy//bazel:windows_x86_64": _filter_windows_keys(cache_entries_debug),
            "@envoy//bazel:opt_build": cache_entries,
            "//conditions:default": cache_entries_debug,
        }),
        cmake_options = cmake_options,
        generate_crosstool_file = select({
            "@envoy//bazel:windows_x86_64": True,
            "//conditions:default": False,
        }),
        lib_source = lib_source,
        make_commands = make_commands,
        postfix_script = pf,
        static_libraries = static_libraries,
        **kwargs
    )

# Used to select a dependency that has different implementations on POSIX vs Windows.
# The platform-specific implementations should be specified with envoy_cc_posix_library
# and envoy_cc_win32_library respectively
def envoy_cc_platform_dep(name):
    return select({
        "@envoy//bazel:windows_x86_64": [name + "_win32"],
        "//conditions:default": [name + "_posix"],
    })

# Envoy proto descriptor targets should be specified with this function.
# This is used for testing only.
def envoy_proto_descriptor(name, out, srcs = [], external_deps = []):
    input_files = ["$(location " + src + ")" for src in srcs]
    include_paths = [".", native.package_name()]

    if "api_httpbody_protos" in external_deps:
        srcs.append("@googleapis//:api_httpbody_protos_src")
        include_paths.append("external/googleapis")

    if "http_api_protos" in external_deps:
        srcs.append("@googleapis//:http_api_protos_src")
        include_paths.append("external/googleapis")

    if "well_known_protos" in external_deps:
        srcs.append("@com_google_protobuf//:well_known_protos")
        include_paths.append("external/com_google_protobuf/src")

    options = ["--include_imports"]
    options.extend(["-I" + include_path for include_path in include_paths])
    options.append("--descriptor_set_out=$@")

    cmd = "$(location //external:protoc) " + " ".join(options + input_files)
    native.genrule(
        name = name,
        srcs = srcs,
        outs = [out],
        cmd = cmd,
        tools = ["//external:protoc"],
    )

# Dependencies on Google grpc should be wrapped with this function.
def envoy_google_grpc_external_deps():
    return envoy_select_google_grpc([envoy_external_dep_path("grpc")])

def envoy_select_boringssl(if_fips, default = None):
    return select({
        "@envoy//bazel:boringssl_fips": if_fips,
        "//conditions:default": default or [],
    })

# Here we create wrappers for each of the public targets within the separate bazel
# files loaded above. This is necessary so that consumers who had previously used
# macros imported from envoy_build_system.bzl don't have to change their BUILD
# files to reference the new location.

# Select wrappers (from envoy_build.bzl)
envoy_select_hot_restart = _envoy_select_hot_restart
envoy_select_google_grpc = _envoy_select_google_grpc

# Binary wrappers (from envoy_binary.bzl)
envoy_cc_binary = _envoy_cc_binary

# Library wrappers (from envoy_library.bzl)
envoy_basic_cc_library = _envoy_basic_cc_library
envoy_cc_library = _envoy_cc_library
envoy_cc_posix_library = _envoy_cc_posix_library
envoy_cc_win32_library = _envoy_cc_win32_library
envoy_include_prefix = _envoy_include_prefix
envoy_proto_library = _envoy_proto_library

# Test wrappers (from envoy_test.bzl)
envoy_cc_fuzz_test = _envoy_cc_fuzz_test
envoy_cc_mock = _envoy_cc_mock
envoy_cc_test = _envoy_cc_test
envoy_cc_test_binary = _envoy_cc_test_binary
envoy_cc_test_library = _envoy_cc_test_library
envoy_py_test_binary = _envoy_py_test_binary
envoy_sh_test = _envoy_sh_test
