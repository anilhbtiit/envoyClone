# This should match the schema defined in external_deps.bzl.
REPOSITORY_LOCATIONS_SPEC = dict(
    bazel_skylib = dict(
        project_name = "bazel-skylib",
        project_desc = "Common useful functions and rules for Bazel",
        project_url = "https://github.com/bazelbuild/bazel-skylib",
        version = "1.1.1",
        sha256 = "c6966ec828da198c5d9adbaa94c05e3a1c7f21bd012a0b29ba8ddbccb2c93b0d",
        release_date = "2021-09-27",
        urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/{version}/bazel-skylib-{version}.tar.gz"],
        use_category = ["api"],
    ),
    rules_dotnet_skylib = dict(
        project_name = "rules_dotnet_skylib",
        project_desc = "Common useful functions and rules for Bazel",
        project_url = "https://github.com/bazelbuild/bazel-skylib",
        version = "1.0.2",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.0.2/bazel-skylib-1.0.2.tar.gz",
            "https://github.com/bazelbuild/bazel-skylib/releases/download/1.0.2/bazel-skylib-1.0.2.tar.gz",
        ],
        release_date = "2019-10-09",
        sha256 = "97e70364e9249702246c0e9444bccdc4b847bed1eb03c5a3ece4f83dfe6abc44",
        use_category = ["api"],
    ),
    com_envoyproxy_protoc_gen_validate = dict(
        project_name = "protoc-gen-validate (PGV)",
        project_desc = "protoc plugin to generate polyglot message validators",
        project_url = "https://github.com/envoyproxy/protoc-gen-validate",
        version = "0.6.2",
        sha256 = "b02da533c77023238c556982507b9a71afc850478b637a7a13ec13f311efa5c0",
        release_date = "2021-10-21",
        strip_prefix = "protoc-gen-validate-{version}",
        urls = ["https://github.com/envoyproxy/protoc-gen-validate/archive/v{version}.tar.gz"],
        use_category = ["api"],
        implied_untracked_deps = [
            "com_github_iancoleman_strcase",
            "com_github_lyft_protoc_gen_star",
            "com_github_spf13_afero",
            "org_golang_google_genproto",
            "org_golang_x_text",
        ],
    ),
    com_github_bazelbuild_buildtools = dict(
        project_name = "Bazel build tools",
        project_desc = "Developer tools for working with Google's bazel buildtool.",
        project_url = "https://github.com/bazelbuild/buildtools",
        version = "4.2.2",
        sha256 = "ae34c344514e08c23e90da0e2d6cb700fcd28e80c02e23e4d5715dddcb42f7b3",
        release_date = "2021-10-07",
        strip_prefix = "buildtools-{version}",
        urls = ["https://github.com/bazelbuild/buildtools/archive/{version}.tar.gz"],
        use_category = ["api"],
    ),
    com_github_cncf_udpa = dict(
        project_name = "xDS API",
        project_desc = "xDS API Working Group (xDS-WG)",
        project_url = "https://github.com/cncf/xds",
        # During the UDPA -> xDS migration, we aren't working with releases.
        version = "cb28da3451f158a947dfc45090fe92b07b243bc1",
        sha256 = "5bc8365613fe2f8ce6cc33959b7667b13b7fe56cb9d16ba740c06e1a7c4242fc",
        release_date = "2021-10-11",
        strip_prefix = "xds-{version}",
        urls = ["https://github.com/cncf/xds/archive/{version}.tar.gz"],
        use_category = ["api"],
    ),
    com_github_openzipkin_zipkinapi = dict(
        project_name = "Zipkin API",
        project_desc = "Zipkin's language independent model and HTTP Api Definitions",
        project_url = "https://github.com/openzipkin/zipkin-api",
        version = "1.0.0",
        sha256 = "6c8ee2014cf0746ba452e5f2c01f038df60e85eb2d910b226f9aa27ddc0e44cf",
        release_date = "2020-11-22",
        strip_prefix = "zipkin-api-{version}",
        urls = ["https://github.com/openzipkin/zipkin-api/archive/{version}.tar.gz"],
        use_category = ["api"],
    ),
    com_google_googleapis = dict(
        # TODO(dio): Consider writing a Starlark macro for importing Google API proto.
        project_name = "Google APIs",
        project_desc = "Public interface definitions of Google APIs",
        project_url = "https://github.com/googleapis/googleapis",
        version = "82944da21578a53b74e547774cf62ed31a05b841",
        sha256 = "a45019af4d3290f02eaeb1ce10990166978c807cb33a9692141a076ba46d1405",
        release_date = "2019-12-02",
        strip_prefix = "googleapis-{version}",
        urls = ["https://github.com/googleapis/googleapis/archive/{version}.tar.gz"],
        use_category = ["api"],
    ),
    rules_proto_grpc = dict(
        project_name = "Protobuf and gRPC rules for Bazel",
        project_desc = "Bazel rules for building Protobuf and gRPC code and libraries from proto_library targets",
        project_url = "https://github.com/rules-proto-grpc/rules_proto_grpc",
        version = "4.0.1",
        sha256 = "28724736b7ff49a48cb4b2b8cfa373f89edfcb9e8e492a8d5ab60aa3459314c8",
        release_date = "2021-10-04",
        strip_prefix = "rules_proto_grpc-{version}",
        urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/archive/v{version}.tar.gz"],
        use_category = ["api"],
    ),
    io_bazel_rules_dotnet = dict(
        project_name = ".Net Rules for Bazel",
        project_desc = "This is a minimal viable set of C#/F# bindings for building C#/F# code with Core",
        project_url = "https://github.com/bazelbuild/rules_dotnet",
        version = "0.0.7",
        sha256 = "322d341066165267d3f411fe9e4f5c3e6630a3cab9c41eafd4197a2e798bbf2a",
        release_date = "2021-02-21",
        strip_prefix = "rules_dotnet-{version}",
        urls = ["https://github.com/bazelbuild/rules_dotnet/archive/refs/tags/{version}.tar.gz"],
        use_category = ["build", "api"],
    ),
    opencensus_proto = dict(
        project_name = "OpenCensus Proto",
        project_desc = "Language Independent Interface Types For OpenCensus",
        project_url = "https://github.com/census-instrumentation/opencensus-proto",
        version = "0.3.0",
        sha256 = "b7e13f0b4259e80c3070b583c2f39e53153085a6918718b1c710caf7037572b0",
        release_date = "2020-07-21",
        strip_prefix = "opencensus-proto-{version}/src",
        urls = ["https://github.com/census-instrumentation/opencensus-proto/archive/v{version}.tar.gz"],
        use_category = ["api"],
    ),
    prometheus_metrics_model = dict(
        project_name = "Prometheus client model",
        project_desc = "Data model artifacts for Prometheus",
        project_url = "https://github.com/prometheus/client_model",
        version = "147c58e9608a4f9628b53b6cc863325ca746f63a",
        sha256 = "f7da30879dcdfae367fa65af1969945c3148cfbfc462b30b7d36f17134675047",
        release_date = "2021-06-07",
        strip_prefix = "client_model-{version}",
        urls = ["https://github.com/prometheus/client_model/archive/{version}.tar.gz"],
        use_category = ["api"],
    ),
    rules_proto = dict(
        project_name = "Protobuf Rules for Bazel",
        project_desc = "Protocol buffer rules for Bazel",
        project_url = "https://github.com/bazelbuild/rules_proto",
        version = "4.0.0",
        sha256 = "66bfdf8782796239d3875d37e7de19b1d94301e8972b3cbd2446b332429b4df1",
        release_date = "2021-09-15",
        strip_prefix = "rules_proto-{version}",
        urls = ["https://github.com/bazelbuild/rules_proto/archive/refs/tags/{version}.tar.gz"],
        use_category = ["api"],
    ),
    opentelemetry_proto = dict(
        project_name = "OpenTelemetry Proto",
        project_desc = "Language Independent Interface Types For OpenTelemetry",
        project_url = "https://github.com/open-telemetry/opentelemetry-proto",
        version = "0.11.0",
        sha256 = "985367f8905e91018e636cbf0d83ab3f834b665c4f5899a27d10cae9657710e2",
        release_date = "2021-10-07",
        strip_prefix = "opentelemetry-proto-{version}",
        urls = ["https://github.com/open-telemetry/opentelemetry-proto/archive/v{version}.tar.gz"],
        use_category = ["api"],
    ),
    com_github_bufbuild_buf = dict(
        project_name = "buf",
        project_desc = "A new way of working with Protocol Buffers.",  # Used for breaking change detection in API protobufs
        project_url = "https://buf.build",
        version = "0.53.0",
        sha256 = "888bb52d358e34a8d6a57ecff426bed896bdf478ad13c78a70a9e1a9a2d75715",
        strip_prefix = "buf",
        urls = ["https://github.com/bufbuild/buf/releases/download/v{version}/buf-Linux-x86_64.tar.gz"],
        release_date = "2021-08-25",
        use_category = ["api"],
        tags = ["manual"],
    ),
    io_bazel_stardoc = dict(
        project_name = "Stardoc - Starlark Documentation Generator",
        project_desc = "Stardoc is a documentation generator for Bazel build rules written in Starlark.",
        project_url = "https://github.com/bazelbuild/stardoc",
        version = "0.5.0",
        sha256 = "c9794dcc8026a30ff67cf7cf91ebe245ca294b20b071845d12c192afe243ad72",
        strip_prefix = "stardoc",
        urls = ["https://github.com/bazelbuild/stardoc/releases/download/{version}/stardoc-{version}.tar.gz"],
        use_category = ["other"],
        release_date = "2021-10-06",
    ),
)
