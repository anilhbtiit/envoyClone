#!/usr/bin/env python
from subprocess import check_output
from subprocess import call

import glob
import os
import shutil
import sys

# Find the locations of the workspace root and the generated files directory.
workspace = check_output(['bazel', 'info', 'workspace']).strip()
bazel_bin = check_output(['bazel', 'info', 'bazel-bin']).strip()
targets = '@envoy_api//...'
import_base = 'github.com/envoyproxy/data-plane-api/api'
output_base = 'go_out'

go_protos = check_output([
    'bazel',
    'query',
    'kind("go_proto_library", %s)' % targets,
]).split()

# Each rule has the form @envoy_api//foo/bar:baz_go_proto.
# First build all the rules to ensure we have the output files.
if call(['bazel', 'build'] + go_protos) != 0:
  print('Build failed')
  sys.exit(1)

shutil.rmtree(os.path.join(workspace, output_base, 'envoy'), ignore_errors=True)
for rule in go_protos:
  # Example rule:
  # @envoy_api//envoy/config/bootstrap/v2:pkg_go_proto
  #
  # Example generated directory:
  # bazel-bin/external/envoy_api/envoy/config/bootstrap/v2/linux_amd64_stripped/pkg_go_proto%/github.com/envoyproxy/data-plane-api/api/envoy/config/bootstrap/v2/
  #
  # Example output directory:
  # go_out/envoy/config/bootstrap/v2
  rule_dir, proto = rule[len("@envoy_api//"):].rsplit(':', 1)

  input_dir = os.path.join(bazel_bin, 'external', 'envoy_api', rule_dir, 'linux_amd64_stripped',
                           proto + '%', import_base, rule_dir)
  input_files = glob.glob(os.path.join(input_dir, '*.go'))
  output_dir = os.path.join(workspace, output_base, rule_dir)

  # Ensure the output directory exists
  if not os.path.exists(output_dir):
    os.makedirs(output_dir, 0o755)
  for generated_file in input_files:
    shutil.copy(generated_file, output_dir)
    os.chmod(os.path.join(output_dir, generated_file), 0o644)
