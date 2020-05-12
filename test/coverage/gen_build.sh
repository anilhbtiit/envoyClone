#!/bin/bash

# Generate test/coverage/BUILD, which contains a single envoy_cc_test target
# that contains all C++ based tests suitable for performing the coverage run. A
# single binary (as opposed to multiple test targets) is require to work around
# the crazy in https://github.com/bazelbuild/bazel/issues/1118. This is used by
# the coverage runner script.

set -e

[ -z "${BAZEL_BIN}" ] && BAZEL_BIN=bazel
[ -z "${BUILDIFIER_BIN}" ] && BUILDIFIER_BIN=buildifier

# Path to the generated BUILD file for the coverage target.
[ -z "${BUILD_PATH}" ] && BUILD_PATH="$(dirname "$0")"/BUILD

# Extra repository information to include when generating coverage targets. This is useful for
# consuming projects. E.g., "@envoy".
[ -z "${REPOSITORY}" ] && REPOSITORY=""

# This is an extra bazel path to query for additional targets. This is useful for consuming projects
# that want to run coverage over the public envoy code as well as private extensions.
# E.g., "//envoy-lyft/test/..."
[ -z "${EXTRA_QUERY_PATHS}" ] && EXTRA_QUERY_PATHS=""

rm -f "${BUILD_PATH}"

if [[ $# -gt 0 ]]; then
  COVERAGE_TARGETS=$*
else
  COVERAGE_TARGETS=//test/...
fi

echo COVERAGE_TARGETS=$COVERAGE_TARGETS

# This setting allows consuming projects to only run coverage over private extensions.
if [[ -z "${ONLY_EXTRA_QUERY_PATHS}" ]]; then
  echo in if...
  for target in ${COVERAGE_TARGETS}; do
    echo checking $target
    TARGETS="$TARGETS $("${BAZEL_BIN}" query ${BAZEL_QUERY_OPTIONS} "attr('tags', 'coverage_test_lib', ${REPOSITORY}${target})" | grep "^//")"
  done

  echo checking quiche

  # Run the QUICHE platform api tests for coverage.
  if [[ "${COVERAGE_TARGETS}" == "//test/..." ]]; then
    TARGETS="$TARGETS $("${BAZEL_BIN}" query ${BAZEL_QUERY_OPTIONS} "attr('tags', 'coverage_test_lib', '@com_googlesource_quiche//:all')" | grep "^@com_googlesource_quiche")"
  fi

  echo done with quiche
fi

if [ -n "${EXTRA_QUERY_PATHS}" ]; then
  TARGETS="$TARGETS $("${BAZEL_BIN}" query ${BAZEL_QUERY_OPTIONS} "attr('tags', 'coverage_test_lib', ${EXTRA_QUERY_PATHS})" | grep "^//")"
fi

echo Generating $BUILD_PATH

(
  cat << EOF
# This file is generated by test/coverage/gen_build.sh automatically prior to
# coverage runs. It is under .gitignore. DO NOT EDIT, DO NOT CHECK IN.
load(
    "${REPOSITORY}//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "coverage_tests",
    repository = "${REPOSITORY}",
    deps = [
EOF
  for t in ${TARGETS}
  do
    echo "        \"$t\","
  done
  cat << EOF
    ],
    # no-remote due to https://github.com/bazelbuild/bazel/issues/4685
    tags = ["manual", "no-remote"],
    coverage = False,
    # Due to the nature of coverage_tests, the shard of coverage_tests are very uneven, some of
    # shard can take 100s and some takes only 10s, so we use the maximum sharding to here to let
    # Bazel scheduling them across CPU cores.
    # Sharding can be disabled by --test_sharding_strategy=disabled.
    shard_count = 50,
)
EOF

) > "${BUILD_PATH}"

echo "Generated coverage BUILD file at: ${BUILD_PATH}"
"${BUILDIFIER_BIN}" "${BUILD_PATH}"
