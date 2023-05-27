workspace(name = "envoy")

load("//bazel:api_binding.bzl", "envoy_api_binding")

envoy_api_binding()

load("//bazel:api_repositories.bzl", "envoy_api_dependencies")

envoy_api_dependencies()

load("//bazel:repositories.bzl", "envoy_dependencies")

envoy_dependencies()

load("//bazel:repositories_extra.bzl", "envoy_dependencies_extra")

envoy_dependencies_extra()

load("//bazel:python_dependencies.bzl", "envoy_python_dependencies")

envoy_python_dependencies()

load("//bazel:dependency_imports.bzl", "envoy_dependency_imports")

envoy_dependency_imports()

# TODO: could these be moved into a dedicated file to avoid bloating the WORKSPACE?
load("//tools/protojsonschema:com_github_chrusty_protoc_gen_jsonschema_deps.bzl", "go_dependencies")
load("@rules_proto_grpc//:repositories.bzl", "rules_proto_grpc_toolchains")

# gazelle:repository_macro //tools/protojsonschema/com_github_chrusty_protoc_gen_jsonschema_deps.bzl%go_dependencies
go_dependencies()

rules_proto_grpc_toolchains()
