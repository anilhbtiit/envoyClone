#!/usr/bin/env python
"""Wrapper for Envoy command-line that collects stats/log/profile.

Example use:

  ./tools/envoy_collect.py --output-path=./envoy.tar -c
  ./configs/google_com_proxy.json --service-node foo
  <Ctrl-C>
  tar -tvf ./envoy.tar
  -rw------- htuch/eng         0 2017-08-13 21:13 access_0.log
  -rw------- htuch/eng       876 2017-08-13 21:13 clusters.txt
  -rw------- htuch/eng        19 2017-08-13 21:13 listeners.txt
  -rw------- htuch/eng        70 2017-08-13 21:13 server_info.txt
  -rw------- htuch/eng      8443 2017-08-13 21:13 stats.txt
  -rw------- htuch/eng      1551 2017-08-13 21:13 config.json
  -rw------- htuch/eng     72432 2017-08-13 21:13 perf.data
  -rw------- htuch/eng     32681 2017-08-13 21:13 envoy.log

The Envoy process will execute as normal and will terminate when interrupted
with SIGINT (ctrl-c on stdin), collecting the various stats/log/profile in the
--output-path tar ball.

TODO(htuch):
  - Add different modes for collecting for perf vs. debug. We don't want access
    logging or -l trace for perf, don't care about profile data in debug.
  - Generate the full perf trace as well, since we may have a different version
    of perf local vs. remote.
  - Writeup some MD docs for GitHub.
  - Add a Bazel run wrapper.
  - Flamegraph generation in post-processing.
  - Support other modes of data collection (e.g. snapshotting on SIGUSR,
    periodic).
  - Consider handling other signals.
  - bz2 compress tar ball.
"""
from __future__ import print_function

import argparse
import ctypes
import ctypes.util
import datetime
import json
import os
import pipes
import shutil
import signal
import subprocess as sp
import sys
import tarfile
import tempfile
from six.moves import urllib

ENVOY_PATH = os.getenv(
    'ENVOY_PATH',
    'bazel-out/local-fastbuild/genfiles/source/exe/envoy-static.stamped')
PERF_PATH = os.getenv('PERF_PATH', 'perf')

PR_SET_PDEATHSIG = 1  # See prtcl(2).

DUMP_HANDLERS = ['clusters', 'listeners', 'server_info', 'stats']


def FetchUrl(url):
  return urllib.request.urlopen(url).read().decode('utf-8')


def ModifyEnvoyconfig(config_path, output_directory):
  # Load original Envoy config.
  with open(config_path, 'r') as f:
    envoy_config = json.loads(f.read())

  # Add unconditional access logs for all listeners.
  access_log_paths = []
  for n, listener in enumerate(envoy_config['listeners']):
    for network_filter in listener['filters']:
      if network_filter['name'] == 'http_connection_manager':
        config = network_filter['config']
        access_log_path = os.path.join(output_directory, 'access_%d.log' % n)
        access_log_config = {'path': access_log_path}
        if 'access_log' in config:
          config['access_log'].append(access_log_config)
        else:
          config['access_log'] = [access_log_config]
        access_log_paths.append(access_log_path)

  # Write out modified Envoy config.
  modified_envoy_config_path = os.path.join(output_directory, 'config.json')
  with open(modified_envoy_config_path, 'w') as f:
    f.write(json.dumps(envoy_config, indent=2))

  return modified_envoy_config_path, access_log_paths


def EnvoyCollect(parse_result, unknown_args):
  envoy_tmpdir = tempfile.mkdtemp()
  manifest = []
  return_code = 1
  try:
    # Setup Envoy config and determine the paths of the files we're going to
    # generate.
    modified_envoy_config_path, access_log_paths = ModifyEnvoyconfig(
        parse_result.config_path, envoy_tmpdir)
    dump_handlers_paths = {
        h: os.path.join(envoy_tmpdir, '%s.txt' % h)
        for h in DUMP_HANDLERS
    }
    perf_data_path = os.path.join(envoy_tmpdir, 'perf.data')
    envoy_log_path = os.path.join(envoy_tmpdir, 'envoy.log')
    # The manifest of files that will be placed in the output .tar.
    manifest = access_log_paths + list(dump_handlers_paths.values()) + [
        modified_envoy_config_path, perf_data_path, envoy_log_path
    ]

    # This is where we will find out where the admin endpoint is listening.
    admin_address_path = os.path.join(envoy_tmpdir, 'admin_address.txt')

    # This is how we will invoke the wrapped envoy.
    # TODO(htuch): Only run under perf when we want a profile, not during debug.
    envoy_shcmd = ' '.join(
        map(pipes.quote, [
            PERF_PATH,
            'record',
            '-o',
            perf_data_path,
            '-g',
            '--',
            ENVOY_PATH,
            '-c',
            modified_envoy_config_path,
            '-l',
            'trace',
            '--admin-address-path',
            admin_address_path,
        ] + unknown_args[1:]))
    print(envoy_shcmd)

    # Some process setup stuff to ensure the child process gets cleaned up properly if the
    # collector dies and doesn't get its signals implicity.
    libc = ctypes.CDLL(ctypes.util.find_library('c'), use_errno=True)

    def EnvoyPreexecFn():
      os.setpgrp()
      libc.prctl(PR_SET_PDEATHSIG, signal.SIGTERM)

    # Launch Envoy, and register for SIGINT.
    with open(envoy_log_path, 'w') as envoy_log:
      envoy_proc = sp.Popen(
          envoy_shcmd,
          stdin=sp.PIPE,
          stderr=envoy_log,
          preexec_fn=EnvoyPreexecFn,
          shell=True)

      def SignalHandler(signum, frame):
        with open(admin_address_path, 'r') as f:
          admin_address = 'http://%s' % f.read()
        for handler, path in dump_handlers_paths.items():
          with open(path, 'w') as f:
            f.write(FetchUrl('%s/%s' % (admin_address, handler)))
        print('Sending Envoy process (PID=%d) SIGINT...' % envoy_proc.pid)
        envoy_proc.send_signal(signal.SIGINT)

      signal.signal(signal.SIGINT, SignalHandler)
      return_code = envoy_proc.wait()

    # Collect manifest files and tar them.
    with tarfile.TarFile(parse_result.output_path, 'w') as output_tar:
      for path in manifest:
        if os.path.exists(path):
          output_tar.add(path, arcname=os.path.basename(path))

    with open(envoy_log_path, 'r') as f:
      sys.stderr.write(f.read())
  finally:
    shutil.rmtree(envoy_tmpdir)
  return return_code


if __name__ == '__main__':
  parser = argparse.ArgumentParser(
      description='Envoy wrapper to collect stats/log/profile.')
  default_output_path = 'envoy-%s.tar' % datetime.datetime.now().isoformat('-')
  parser.add_argument(
      '--output-path', default=default_output_path, help='path to output .tar.')
  # We either need to interpret or override these, so we declare them in
  # envoy_collect.py and always parse and present them again when invoking
  # Envoy.
  parser.add_argument(
      '--config-path',
      '-c',
      required=True,
      help='Path to Envoy configuration file.')
  parser.add_argument(
      '--log-level',
      '-l',
      help='Envoy log level. This will be overriden when invoking Envoy.')
  sys.exit(EnvoyCollect(*parser.parse_known_args(sys.argv)))
