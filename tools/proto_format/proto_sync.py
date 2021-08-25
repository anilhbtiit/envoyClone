#!/usr/bin/env python3

# 1. Take protoxform artifacts from Bazel cache and pretty-print with protoprint.py.
# 2. In the case where we are generating an Envoy internal shadow, it may be
#    necessary to combine the current active proto, subject to hand editing, with
#    shadow artifacts from the previous version; this is done via
#    merge_active_shadow.py.
# 3. Diff or copy resulting artifacts to the source tree.

import argparse
from collections import defaultdict
import multiprocessing as mp
import os
import pathlib
import re
import shutil
import string
import subprocess
import sys
import tempfile

from api_proto_plugin import utils

from importlib.util import spec_from_loader, module_from_spec
from importlib.machinery import SourceFileLoader

# api/bazel/external_protos_deps.bzl must have a .bzl suffix for Starlark
# import, so we are forced to this workaround.
_external_proto_deps_spec = spec_from_loader(
    'external_proto_deps',
    SourceFileLoader('external_proto_deps', 'api/bazel/external_proto_deps.bzl'))
external_proto_deps = module_from_spec(_external_proto_deps_spec)
_external_proto_deps_spec.loader.exec_module(external_proto_deps)

# These .proto import direct path prefixes are already handled by
# api_proto_package() as implicit dependencies.
API_BUILD_SYSTEM_IMPORT_PREFIXES = [
    'google/api/annotations.proto',
    'google/protobuf/',
    'google/rpc/status.proto',
    'validate/validate.proto',
]

# Each of the following contrib extensions are allowed to be in the v3 namespace. Indicate why.
CONTRIB_V3_ALLOW_LIST = [
    # Extensions moved from core to contrib.
    'envoy.extensions.filters.http.squash.v3',
    'envoy.extensions.filters.network.kafka_broker.v3',
    'envoy.extensions.filters.network.rocketmq_proxy.v3',
]

BUILD_FILE_TEMPLATE = string.Template(
    """# DO NOT EDIT. This file is generated by tools/proto_format/proto_sync.py.

load("@envoy_api//bazel:api_build_system.bzl", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package($fields)
""")

IGNORED_V2_PROTOS = [
    "envoy/config/accesslog/v2",
    "envoy/config/cluster/aggregate/v2alpha",
    "envoy/config/cluster/dynamic_forward_proxy/v2alpha",
    "envoy/config/cluster/redis",
    "envoy/config/common/dynamic_forward_proxy/v2alpha",
    "envoy/config/common/tap/v2alpha",
    "envoy/config/filter/dubbo/router/v2alpha1",
    "envoy/config/filter/http/adaptive_concurrency/v2alpha",
    "envoy/config/filter/http/aws_lambda/v2alpha",
    "envoy/config/filter/http/aws_request_signing/v2alpha",
    "envoy/config/filter/http/buffer/v2",
    "envoy/config/filter/http/cache/v2alpha",
    "envoy/config/filter/http/compressor/v2",
    "envoy/config/filter/http/cors/v2",
    "envoy/config/filter/http/csrf/v2",
    "envoy/config/filter/http/dynamic_forward_proxy/v2alpha",
    "envoy/config/filter/http/dynamo/v2",
    "envoy/config/filter/http/ext_authz/v2",
    "envoy/config/filter/http/fault/v2",
    "envoy/config/filter/http/grpc_http1_bridge/v2",
    "envoy/config/filter/http/grpc_http1_reverse_bridge/v2alpha1",
    "envoy/config/filter/http/grpc_stats/v2alpha",
    "envoy/config/filter/http/grpc_web/v2",
    "envoy/config/filter/http/gzip/v2",
    "envoy/config/filter/http/header_to_metadata/v2",
    "envoy/config/filter/http/health_check/v2",
    "envoy/config/filter/http/ip_tagging/v2",
    "envoy/config/filter/http/jwt_authn/v2alpha",
    "envoy/config/filter/http/lua/v2",
    "envoy/config/filter/http/on_demand/v2",
    "envoy/config/filter/http/original_src/v2alpha1",
    "envoy/config/filter/http/rate_limit/v2",
    "envoy/config/filter/http/rbac/v2",
    "envoy/config/filter/http/router/v2",
    "envoy/config/filter/http/squash/v2",
    "envoy/config/filter/http/tap/v2alpha",
    "envoy/config/filter/http/transcoder/v2",
    "envoy/config/filter/listener/http_inspector/v2",
    "envoy/config/filter/listener/original_dst/v2",
    "envoy/config/filter/listener/original_src/v2alpha1",
    "envoy/config/filter/listener/proxy_protocol/v2",
    "envoy/config/filter/listener/tls_inspector/v2",
    "envoy/config/filter/network/client_ssl_auth/v2",
    "envoy/config/filter/network/direct_response/v2",
    "envoy/config/filter/network/dubbo_proxy/v2alpha1",
    "envoy/config/filter/network/echo/v2",
    "envoy/config/filter/network/ext_authz/v2",
    "envoy/config/filter/network/kafka_broker/v2alpha1",
    "envoy/config/filter/network/local_rate_limit/v2alpha",
    "envoy/config/filter/network/mongo_proxy/v2",
    "envoy/config/filter/network/mysql_proxy/v1alpha1",
    "envoy/config/filter/network/rate_limit/v2",
    "envoy/config/filter/network/rbac/v2",
    "envoy/config/filter/network/sni_cluster/v2",
    "envoy/config/filter/network/zookeeper_proxy/v1alpha1",
    "envoy/config/filter/thrift/rate_limit/v2alpha1",
    "envoy/config/filter/udp/udp_proxy/v2alpha",
    "envoy/config/grpc_credential/v2alpha",
    "envoy/config/ratelimit/v2",
    "envoy/config/rbac/v2",
    "envoy/config/retry/omit_host_metadata/v2",
    "envoy/config/retry/previous_priorities",
    "envoy/config/transport_socket/raw_buffer/v2",
    "envoy/config/transport_socket/tap/v2alpha",
    "envoy/data/cluster/v2alpha",
    "envoy/data/dns/v2alpha",
    "envoy/data/core/v2alpha",
    "envoy/service/event_reporting/v2alpha",
    "envoy/service/trace/v2",
]

IMPORT_REGEX = re.compile('import "(.*)";')
SERVICE_REGEX = re.compile('service \w+ {')
PACKAGE_REGEX = re.compile('\npackage: "([^"]*)"')
PREVIOUS_MESSAGE_TYPE_REGEX = re.compile(r'previous_message_type\s+=\s+"([^"]*)";')


class ProtoSyncError(Exception):
    pass


class RequiresReformatError(ProtoSyncError):

    def __init__(self, message):
        super(RequiresReformatError, self).__init__(
            '%s; either run ./ci/do_ci.sh fix_format or ./tools/proto_format/proto_format.sh fix to reformat.\n'
            % message)


def get_directory_from_package(package):
    """Get directory path from package name or full qualified message name

    Args:
        package: the full qualified name of package or message.
    """
    return '/'.join(s for s in package.split('.') if s and s[0].islower())


def get_destination_path(src):
    """Obtain destination path from a proto file path by reading its package statement.

    Args:
        src: source path
    """
    src_path = pathlib.Path(src)
    contents = src_path.read_text(encoding='utf8')
    matches = re.findall(PACKAGE_REGEX, contents)
    if len(matches) != 1:
        raise RequiresReformatError(
            "Expect {} has only one package declaration but has {}".format(src, len(matches)))
    package = matches[0]
    dst_path = pathlib.Path(
        get_directory_from_package(package)).joinpath(src_path.name.split('.')[0] + ".proto")
    # contrib API files have the standard namespace but are in a contrib folder for clarity.
    # The following prepends contrib for contrib packages so we wind up with the real final path.
    if 'contrib' in src:
        if 'v3alpha' not in package and 'v4alpha' not in package and package not in CONTRIB_V3_ALLOW_LIST:
            raise ProtoSyncError(
                "contrib extension package '{}' does not use v3alpha namespace. "
                "Add to CONTRIB_V3_ALLOW_LIST with an explanation if this is on purpose.".format(
                    package))

        dst_path = pathlib.Path('contrib').joinpath(dst_path)
    return dst_path


def get_abs_rel_destination_path(dst_root, src):
    """Obtain absolute path from a proto file path combined with destination root.

    Creates the parent directory if necessary.

    Args:
        dst_root: destination root path.
        src: source path.
    """
    rel_dst_path = get_destination_path(src)
    dst = dst_root.joinpath(rel_dst_path)
    dst.parent.mkdir(0o755, parents=True, exist_ok=True)
    return dst, rel_dst_path


def proto_print(src, dst):
    """Pretty-print FileDescriptorProto to a destination file.

    Args:
        src: source path for FileDescriptorProto.
        dst: destination path for formatted proto.
    """
    print('proto_print %s' % dst)
    subprocess.check_output([
        'bazel-bin/tools/protoxform/protoprint', src,
        str(dst),
        './bazel-bin/tools/protoxform/protoprint.runfiles/envoy/tools/type_whisperer/api_type_db.pb_text',
        'API_VERSION'
    ])


def merge_active_shadow(active_src, shadow_src, dst):
    """Merge active/shadow FileDescriptorProto to a destination file.

    Args:
        active_src: source path for active FileDescriptorProto.
        shadow_src: source path for active FileDescriptorProto.
        dst: destination path for FileDescriptorProto.
    """
    print('merge_active_shadow %s' % dst)
    subprocess.check_output([
        'bazel-bin/tools/protoxform/merge_active_shadow',
        active_src,
        shadow_src,
        dst,
    ])


def sync_proto_file(dst_srcs):
    """Pretty-print a proto descriptor from protoxform.py Bazel cache artifacts."

    In the case where we are generating an Envoy internal shadow, it may be
    necessary to combine the current active proto, subject to hand editing, with
    shadow artifacts from the previous verion; this is done via
    merge_active_shadow().

    Args:
        dst_srcs: destination/sources path tuple.
    """
    dst, srcs = dst_srcs
    assert (len(srcs) > 0)
    # If we only have one candidate source for a destination, just pretty-print.
    if len(srcs) == 1:
        src = srcs[0]
        proto_print(src, dst)
    else:
        # We should only see an active and next major version candidate from
        # previous version today.
        assert (len(srcs) == 2)
        shadow_srcs = [
            s for s in srcs if s.endswith('.next_major_version_candidate.envoy_internal.proto')
        ]
        active_src = [s for s in srcs if s.endswith('active_or_frozen.proto')][0]
        # If we're building the shadow, we need to combine the next major version
        # candidate shadow with the potentially hand edited active version.
        if len(shadow_srcs) > 0:
            assert (len(shadow_srcs) == 1)
            with tempfile.NamedTemporaryFile() as f:
                merge_active_shadow(active_src, shadow_srcs[0], f.name)
                proto_print(f.name, dst)
        else:
            proto_print(active_src, dst)
        src = active_src
    rel_dst_path = get_destination_path(src)
    return ['//%s:pkg' % str(rel_dst_path.parent)]


def get_import_deps(proto_path):
    """Obtain the Bazel dependencies for the import paths from a .proto file.

    Args:
        proto_path: path to .proto.

    Returns:
        A list of Bazel targets reflecting the imports in the .proto at proto_path.
    """
    imports = []
    with open(proto_path, 'r', encoding='utf8') as f:
        for line in f:
            match = re.match(IMPORT_REGEX, line)
            if match:
                import_path = match.group(1)
                # We can ignore imports provided implicitly by api_proto_package().
                if any(import_path.startswith(p) for p in API_BUILD_SYSTEM_IMPORT_PREFIXES):
                    continue
                # Special case handling for UDPA annotations.
                if import_path.startswith('udpa/annotations/'):
                    imports.append('@com_github_cncf_udpa//udpa/annotations:pkg')
                    continue
                if import_path.startswith('xds/type/matcher/v3/'):
                    imports.append('@com_github_cncf_udpa//xds/type/matcher/v3:pkg')
                    continue
                # Special case handling for UDPA core.
                if import_path.startswith('xds/core/v3/'):
                    imports.append('@com_github_cncf_udpa//xds/core/v3:pkg')
                    continue
                # Explicit remapping for external deps, compute paths for envoy/*.
                if import_path in external_proto_deps.EXTERNAL_PROTO_IMPORT_BAZEL_DEP_MAP:
                    imports.append(
                        external_proto_deps.EXTERNAL_PROTO_IMPORT_BAZEL_DEP_MAP[import_path])
                    continue
                if import_path.startswith('envoy/') or import_path.startswith('contrib/'):
                    # Ignore package internal imports.
                    if os.path.dirname(proto_path).endswith(os.path.dirname(import_path)):
                        continue
                    imports.append('//%s:pkg' % os.path.dirname(import_path))
                    continue
                raise ProtoSyncError(
                    'Unknown import path mapping for %s, please update the mappings in tools/proto_format/proto_sync.py.\n'
                    % import_path)
    return imports


def get_previous_message_type_deps(proto_path):
    """Obtain the Bazel dependencies for the previous version of messages in a .proto file.

    We need to link in earlier proto descriptors to support Envoy reflection upgrades.

    Args:
        proto_path: path to .proto.

    Returns:
        A list of Bazel targets reflecting the previous message types in the .proto at proto_path.
    """
    contents = pathlib.Path(proto_path).read_text(encoding='utf8')
    matches = re.findall(PREVIOUS_MESSAGE_TYPE_REGEX, contents)
    deps = []
    for m in matches:
        pkg = get_directory_from_package(m)
        if pkg in IGNORED_V2_PROTOS:
            continue

        if 'contrib' in proto_path:
            pkg = 'contrib/%s' % pkg

        deps.append('//%s:pkg' % pkg)
    return deps


def has_services(proto_path):
    """Does a .proto file have any service definitions?

    Args:
        proto_path: path to .proto.

    Returns:
        True iff there are service definitions in the .proto at proto_path.
    """
    with open(proto_path, 'r', encoding='utf8') as f:
        for line in f:
            if re.match(SERVICE_REGEX, line):
                return True
    return False


# Key sort function to achieve consistent results with buildifier.
def build_order_key(key):
    return key.replace(':', '!')


def build_file_contents(root, files):
    """Compute the canonical BUILD contents for an api/ proto directory.

    Args:
        root: base path to directory.
        files: a list of files in the directory.

    Returns:
        A string containing the canonical BUILD file content for root.
    """
    import_deps = set(sum([get_import_deps(os.path.join(root, f)) for f in files], []))
    history_deps = set(
        sum([get_previous_message_type_deps(os.path.join(root, f)) for f in files], []))
    deps = import_deps.union(history_deps)
    _has_services = any(has_services(os.path.join(root, f)) for f in files)
    fields = []
    if _has_services:
        fields.append('    has_services = True,')
    if deps:
        if len(deps) == 1:
            formatted_deps = '"%s"' % list(deps)[0]
        else:
            formatted_deps = '\n' + '\n'.join(
                '        "%s",' % dep for dep in sorted(deps, key=build_order_key)) + '\n    '
        fields.append('    deps = [%s],' % formatted_deps)
    formatted_fields = '\n' + '\n'.join(fields) + '\n' if fields else ''
    return BUILD_FILE_TEMPLATE.substitute(fields=formatted_fields)


def sync_build_files(cmd, dst_root):
    """Diff or in-place update api/ BUILD files.

    Args:
        cmd: 'check' or 'fix'.
    """
    for root, dirs, files in os.walk(str(dst_root)):
        is_proto_dir = any(f.endswith('.proto') for f in files)
        if not is_proto_dir:
            continue
        build_contents = build_file_contents(root, files)
        build_path = os.path.join(root, 'BUILD')
        with open(build_path, 'w') as f:
            f.write(build_contents)


def generate_current_api_dir(api_dir, dst_dir):
    """Helper function to generate original API repository to be compared with diff.
    This copies the original API repository and deletes file we don't want to compare.

    Args:
        api_dir: the original api directory
        dst_dir: the api directory to be compared in temporary directory
    """
    contrib_dst = dst_dir.joinpath("contrib")
    shutil.copytree(str(api_dir.joinpath("contrib")), str(contrib_dst))

    dst = dst_dir.joinpath("envoy")
    shutil.copytree(str(api_dir.joinpath("envoy")), str(dst))

    for p in dst.glob('**/*.md'):
        p.unlink()
    # envoy.service.auth.v2alpha exist for compatibility while we don't run in protoxform
    # so we ignore it here.
    shutil.rmtree(str(dst.joinpath("service", "auth", "v2alpha")))

    for proto in IGNORED_V2_PROTOS:
        shutil.rmtree(str(dst.joinpath(proto[6:])))


def git_status(path):
    return subprocess.check_output(['git', 'status', '--porcelain', str(path)]).decode()


def git_modified_files(path, suffix):
    """Obtain a list of modified files since the last commit merged by GitHub.

    Args:
        path: path to examine.
        suffix: path suffix to filter with.
    Return:
        A list of strings providing the paths of modified files in the repo.
    """
    try:
        modified_files = subprocess.check_output(
            ['tools/git/modified_since_last_github_commit.sh', 'api', 'proto']).decode().split()
        return modified_files
    except subprocess.CalledProcessError as e:
        if e.returncode == 1:
            return []
        raise


# If we're not forcing format, i.e. FORCE_PROTO_FORMAT=yes, in the environment,
# then try and see if we can skip reformatting based on some simple path
# heuristics. This saves a ton of time, since proto format and sync is not
# running under Bazel and can't do change detection.
def should_sync(path, api_proto_modified_files, py_tools_modified_files):
    if os.getenv('FORCE_PROTO_FORMAT') == 'yes':
        return True
    # If tools change, safest thing to do is rebuild everything.
    if len(py_tools_modified_files) > 0:
        return True
    # Check to see if the basename of the file has been modified since the last
    # GitHub commit. If so, rebuild. This is safe and conservative across package
    # migrations in v3 and v4alpha; we could achieve a lower rate of false
    # positives if we examined package migration annotations, at the expense of
    # complexity.
    for p in api_proto_modified_files:
        if os.path.basename(p) in path:
            return True
    # Otherwise we can safely skip syncing.
    return False


def sync(api_root, mode, is_ci, labels, shadow):
    api_proto_modified_files = git_modified_files('api', 'proto')
    py_tools_modified_files = git_modified_files('tools', 'py')
    with tempfile.TemporaryDirectory() as tmp:
        dst_dir = pathlib.Path(tmp).joinpath("b")
        paths = []
        for label in labels:
            paths.append(utils.bazel_bin_path_for_output_artifact(label, '.active_or_frozen.proto'))
            paths.append(
                utils.bazel_bin_path_for_output_artifact(
                    label, '.next_major_version_candidate.envoy_internal.proto'
                    if shadow else '.next_major_version_candidate.proto'))
        dst_src_paths = defaultdict(list)
        for path in paths:
            if os.path.exists(path) and os.stat(path).st_size > 0:
                abs_dst_path, rel_dst_path = get_abs_rel_destination_path(dst_dir, path)
                if should_sync(path, api_proto_modified_files, py_tools_modified_files):
                    dst_src_paths[abs_dst_path].append(path)
                else:
                    print('Skipping sync of %s' % path)
                    src_path = str(pathlib.Path(api_root, rel_dst_path))
                    shutil.copy(src_path, abs_dst_path)
        with mp.Pool() as p:
            pkg_deps = p.map(sync_proto_file, dst_src_paths.items())
        sync_build_files(mode, dst_dir)

        current_api_dir = pathlib.Path(tmp).joinpath("a")
        current_api_dir.mkdir(0o755, True, True)
        api_root_path = pathlib.Path(api_root)
        generate_current_api_dir(api_root_path, current_api_dir)

        # These support files are handled manually.
        for f in ['envoy/annotations/resource.proto', 'envoy/annotations/deprecation.proto',
                  'envoy/annotations/BUILD']:
            copy_dst_dir = pathlib.Path(dst_dir, os.path.dirname(f))
            copy_dst_dir.mkdir(exist_ok=True)
            shutil.copy(str(pathlib.Path(api_root, f)), str(copy_dst_dir))

        diff = subprocess.run(['diff', '-Npur', "a", "b"], cwd=tmp, stdout=subprocess.PIPE).stdout

        if diff.strip():
            if mode == "check":
                print(
                    "Please apply following patch to directory '{}'".format(api_root),
                    file=sys.stderr)
                print(diff.decode(), file=sys.stderr)
                sys.exit(1)
            if mode == "fix":
                _git_status = git_status(api_root)
                if _git_status:
                    print('git status indicates a dirty API tree:\n%s' % _git_status)
                    print(
                        'Proto formatting may overwrite or delete files in the above list with no git backup.'
                    )
                    if not is_ci and input('Continue? [yN] ').strip().lower() != 'y':
                        sys.exit(1)
                src_files = set(
                    str(p.relative_to(current_api_dir)) for p in current_api_dir.rglob('*'))
                dst_files = set(str(p.relative_to(dst_dir)) for p in dst_dir.rglob('*'))
                deleted_files = src_files.difference(dst_files)
                if deleted_files:
                    print('The following files will be deleted: %s' % sorted(deleted_files))
                    print(
                        'If this is not intended, please see https://github.com/envoyproxy/envoy/blob/main/api/STYLE.md#adding-an-extension-configuration-to-the-api.'
                    )
                    if not is_ci and input('Delete files? [yN] ').strip().lower() != 'y':
                        sys.exit(1)
                    else:
                        subprocess.run(['patch', '-p1'],
                                       input=diff,
                                       cwd=str(api_root_path.resolve()))
                else:
                    subprocess.run(['patch', '-p1'], input=diff, cwd=str(api_root_path.resolve()))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mode', choices=['check', 'fix'])
    parser.add_argument('--api_root', default='./api')
    parser.add_argument('--api_shadow_root', default='./generated_api_shadow')
    parser.add_argument('--ci', action="store_true", default=False)
    parser.add_argument('labels', nargs='*')
    args = parser.parse_args()

    sync(args.api_root, args.mode, args.ci, args.labels, False)
    sync(args.api_shadow_root, args.mode, args.ci, args.labels, True)
