"""Public rules for using cc proto descriptor library with protos:
  - cc_proto_descriptor_library()
"""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain")

# Generic support code #########################################################

# begin:github_only
_is_google3 = False
# end:github_only

# begin:google_only
# _is_google3 = True
# end:google_only

def _get_real_short_path(file):
    # For some reason, files from other archives have short paths that look like:
    #   ../com_google_protobuf/google/protobuf/descriptor.proto
    short_path = file.short_path
    if short_path.startswith("../"):
        second_slash = short_path.index("/", 3)
        short_path = short_path[second_slash + 1:]

    # Sometimes it has another few prefixes like:
    #   _virtual_imports/any_proto/google/protobuf/any.proto
    #   benchmarks/_virtual_imports/100_msgs_proto/benchmarks/100_msgs.proto
    # We want just google/protobuf/any.proto.
    virtual_imports = "_virtual_imports/"
    if virtual_imports in short_path:
        short_path = short_path.split(virtual_imports)[1].split("/", 1)[1]
    return short_path

def _get_real_root(file):
    real_short_path = _get_real_short_path(file)
    return file.path[:-len(real_short_path) - 1]

def _generate_output_file(ctx, src, extension):
    real_short_path = _get_real_short_path(src)
    real_short_path = paths.relativize(real_short_path, ctx.label.package)
    output_filename = paths.replace_extension(real_short_path, extension)

    ret = ctx.actions.declare_file(output_filename)
    return ret

def _filter_none(elems):
    out = []
    for elem in elems:
        if elem:
            out.append(elem)
    return out

def _cc_library_func(ctx, name, hdrs, srcs, copts, dep_ccinfos):
    """Like cc_library(), but callable from rules.

    Args:
      ctx: Rule context.
      name: Unique name used to generate output files.
      hdrs: Public headers that can be #included from other rules.
      srcs: C/C++ source files.
      dep_ccinfos: CcInfo providers of dependencies we should build/link against.

    Returns:
      CcInfo provider for this compilation.
    """

    compilation_contexts = [info.compilation_context for info in dep_ccinfos]
    linking_contexts = [info.linking_context for info in dep_ccinfos]
    toolchain = find_cpp_toolchain(ctx)
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )

    blaze_only_args = {}

    if _is_google3:
        blaze_only_args["grep_includes"] = ctx.file._grep_includes

    (compilation_context, compilation_outputs) = cc_common.compile(
        actions = ctx.actions,
        feature_configuration = feature_configuration,
        cc_toolchain = toolchain,
        name = name,
        srcs = srcs,
        public_hdrs = hdrs,
        user_compile_flags = copts,
        compilation_contexts = compilation_contexts,
        **blaze_only_args
    )
    (linking_context, linking_outputs) = cc_common.create_linking_context_from_compilation_outputs(
        actions = ctx.actions,
        name = name,
        feature_configuration = feature_configuration,
        cc_toolchain = toolchain,
        compilation_outputs = compilation_outputs,
        linking_contexts = linking_contexts,
        **blaze_only_args
    )

    return CcInfo(
        compilation_context = compilation_context,
        linking_context = linking_context,
    )

# Dummy rule to expose select() copts to aspects  ##############################

_CcProtoDescriptorLibraryCopts = provider(
    fields = {
        "copts": "copts for cc_proto_descriptor_library()",
    },
)

def cc_proto_descriptor_library_copts_impl(ctx):
    return _CcProtoDescriptorLibraryCopts(copts = ctx.attr.copts)

cc_proto_descriptor_library_copts = rule(
    implementation = cc_proto_descriptor_library_copts_impl,
    attrs = {"copts": attr.string_list(default = [])},
)

# cc_proto_descriptor_library shared code #################

GeneratedSrcsInfo = provider(
    fields = {
        "srcs": "list of srcs",
        "hdrs": "list of hdrs",
    },
)

_WrappedCcInfo = provider(fields = ["cc_info"])
_WrappedGeneratedSrcsInfo = provider(fields = ["srcs"])

def _compile_protos(ctx, generator, proto_info, proto_sources):
    if len(proto_sources) == 0:
        return GeneratedSrcsInfo(srcs = [], hdrs = [])

    ext = "_" + generator
    tool = ctx.executable._plugin

    srcs = [_generate_output_file(ctx, name, ext + ".pb.cc") for name in proto_sources]
    hdrs = [_generate_output_file(ctx, name, ext + ".pb.h") for name in proto_sources]
    transitive_sets = proto_info.transitive_descriptor_sets.to_list()

    codegen_params = ""
    ctx.actions.run(
        inputs = depset(
            direct = [proto_info.direct_descriptor_set],
            transitive = [proto_info.transitive_descriptor_sets],
        ),
        tools = [tool],
        outputs = srcs + hdrs,
        executable = ctx.executable._protoc,
        arguments = [
                        "--file-descriptor_out=" + _get_real_root(srcs[0]),
                        "--plugin=protoc-gen-file-descriptor=" + ctx.executable._plugin.path,
                        "--descriptor_set_in=" + ctx.configuration.host_path_separator.join([f.path for f in transitive_sets]),
                    ] +
                    [_get_real_short_path(file) for file in proto_sources],
        progress_message = "Generating cc proto descriptor library for :" + ctx.label.name,
        mnemonic = "GenCcProtoDescriptors",
    )
    return GeneratedSrcsInfo(srcs = srcs, hdrs = hdrs)

def _cc_proto_descriptor_library_rule_impl(ctx):
    if len(ctx.attr.deps) != 1:
        fail("only one deps dependency allowed.")
    dep = ctx.attr.deps[0]

    if _WrappedGeneratedSrcsInfo in dep:
        srcs = dep[_WrappedGeneratedSrcsInfo].srcs
    else:
        fail("proto_library rule must generate _WrappedGeneratedSrcsInfo " +
             "(aspect should have handled this).")

    if _WrappedCcInfo in dep:
        cc_info = dep[_WrappedCcInfo].cc_info
    else:
        fail("proto_library rule must generate _WrappedCcInfo " +
             "(aspect should have handled this).")

    lib = cc_info.linking_context.linker_inputs.to_list()[0].libraries[0]
    files = _filter_none([
        lib.static_library,
        lib.pic_static_library,
        lib.dynamic_library,
    ])
    return [
        DefaultInfo(files = depset(files + srcs.hdrs + srcs.srcs)),
        srcs,
        cc_info,
    ]

def _cc_proto_descriptor_library_aspect_impl(target, ctx):
    generator = "descriptor"
    cc_provider = _WrappedCcInfo
    file_provider = _WrappedGeneratedSrcsInfo
    proto_info = target[ProtoInfo]
    proto_sources = proto_info.direct_sources

    files = _compile_protos(ctx, generator, proto_info, proto_sources)
    deps = ctx.rule.attr.deps + getattr(ctx.attr, "_" + generator)
    dep_ccinfos = [dep[CcInfo] for dep in deps if CcInfo in dep]
    dep_ccinfos += [dep[_WrappedCcInfo].cc_info for dep in deps if _WrappedCcInfo in dep]
    cc_info = _cc_library_func(
        ctx = ctx,
        name = ctx.rule.attr.name + "." + generator,
        hdrs = files.hdrs,
        srcs = files.srcs,
        copts = [],
        dep_ccinfos = dep_ccinfos,
    )
    return [cc_provider(cc_info = cc_info), file_provider(srcs = files)]

def _maybe_add(d):
    if _is_google3:
        d["_grep_includes"] = attr.label(
            allow_single_file = True,
            cfg = "exec",
            default = "@bazel_tools//tools/cpp:grep-includes",
        )
    return d

# cc_proto_descriptor_library() ##########################################################

_cc_proto_descriptor_library_aspect = aspect(
    attrs = _maybe_add({
        "_plugin": attr.label(
            executable = True,
            cfg = "exec",
            default = "//bazel/cc_proto_descriptor_library:file_descriptor_generator",
        ),
        "_protoc": attr.label(
            executable = True,
            cfg = "exec",
            default = "@com_google_protobuf//:protoc",
        ),
        "_cc_toolchain": attr.label(
            default = "@bazel_tools//tools/cpp:current_cc_toolchain",
        ),
        "_descriptor": attr.label_list(
            default = [
                Label("//bazel/cc_proto_descriptor_library:file_descriptor_info"),
                Label("@com_google_absl//absl/base:core_headers"),
            ],
        ),
    }),
    implementation = _cc_proto_descriptor_library_aspect_impl,
    provides = [
        _WrappedCcInfo,
        _WrappedGeneratedSrcsInfo,
    ],
    attr_aspects = ["deps"],
    fragments = ["cpp"],
    toolchains = ["@bazel_tools//tools/cpp:toolchain_type"],
    incompatible_use_toolchain_transition = True,
)

cc_proto_descriptor_library = rule(
    output_to_genfiles = True,
    implementation = _cc_proto_descriptor_library_rule_impl,
    attrs = {
        "deps": attr.label_list(
            aspects = [_cc_proto_descriptor_library_aspect],
            allow_rules = ["proto_library"],
            providers = [ProtoInfo],
        ),
    },
)
