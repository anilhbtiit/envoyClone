load("@envoy_mobile//bazel:envoy_mobile_repositories.bzl", "envoy_mobile_repositories")

def _non_module_dependencies_impl(_ctx):
    envoy_mobile_repositories()

non_module_dependencies = module_extension(
    implementation = _non_module_dependencies_impl,
)
