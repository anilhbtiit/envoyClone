#include "test/test_common/environment.h"

#include <sys/un.h>
#include <unistd.h>

#ifdef __has_include
#if __has_include(<experimental/filesystem>)
#include <experimental/filesystem>
#endif
#endif
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/common/assert.h"
#include "common/common/compiler_requirements.h"
#include "common/common/logger.h"
#include "common/common/macros.h"
#include "common/common/utility.h"

#include "server/options_impl.h"

#include "test/test_common/network_utility.h"

#include "absl/strings/match.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace {

std::string makeTempDir(char* name_template) {
#ifdef WIN32
  char* dirname = ::_mktemp(name_template);
  RELEASE_ASSERT(dirname != nullptr,
                 fmt::format("failed to create tempdir: {} {}", dirname, strerror(errno)));
  std::experimental::filesystem::create_directories(dirname);
#else
  char* dirname = ::mkdtemp(name_template);
  RELEASE_ASSERT(dirname != nullptr,
                 fmt::format("failed to create tempdir: {} {}", dirname, strerror(errno)));
#endif
  return std::string(dirname);
}

std::string getOrCreateUnixDomainSocketDirectory() {
  const char* path = std::getenv("TEST_UDSDIR");
  if (path != nullptr) {
    return std::string(path);
  }
  // Generate temporary path for Unix Domain Sockets only. This is a workaround
  // for the sun_path limit on sockaddr_un, since TEST_TMPDIR as generated by
  // Bazel may be too long.
  char test_udsdir[] = "/tmp/envoy_test_uds.XXXXXX";
  return makeTempDir(test_udsdir);
}

std::string getTemporaryDirectory() {
  if (std::getenv("TEST_TMPDIR")) {
    return TestEnvironment::getCheckedEnvVar("TEST_TMPDIR");
  }
  if (std::getenv("TMPDIR")) {
    return TestEnvironment::getCheckedEnvVar("TMPDIR");
  }
  char test_tmpdir[] = "/tmp/envoy_test_tmp.XXXXXX";
  return makeTempDir(test_tmpdir);
}

// Allow initializeOptions() to remember CLI args for getOptions().
int argc_;
char** argv_;

} // namespace

void TestEnvironment::createPath(const std::string& path) {
#ifdef __cpp_lib_experimental_filesystem
  // We don't want to rely on mkdir etc. if we can avoid it, since it might not
  // exist in some environments such as ClusterFuzz.
  std::experimental::filesystem::create_directories(std::experimental::filesystem::path(path));
#else
  // No support on this system for std::experimental::filesystem.
  RELEASE_ASSERT(::system(("mkdir -p " + path).c_str()) == 0, "");
#endif
}

void TestEnvironment::createParentPath(const std::string& path) {
#ifdef __cpp_lib_experimental_filesystem
  // We don't want to rely on mkdir etc. if we can avoid it, since it might not
  // exist in some environments such as ClusterFuzz.
  std::experimental::filesystem::create_directories(
      std::experimental::filesystem::path(path).parent_path());
#else
  // No support on this system for std::experimental::filesystem.
  RELEASE_ASSERT(::system(("mkdir -p $(dirname " + path + ")").c_str()) == 0, "");
#endif
}

void TestEnvironment::removePath(const std::string& path) {
  RELEASE_ASSERT(absl::StartsWith(path, TestEnvironment::temporaryDirectory()), "");
#ifdef __cpp_lib_experimental_filesystem
  // We don't want to rely on rm etc. if we can avoid it, since it might not
  // exist in some environments such as ClusterFuzz.
  if (!std::experimental::filesystem::exists(path)) {
    return;
  }
  std::experimental::filesystem::remove_all(std::experimental::filesystem::path(path));
#else
  // No support on this system for std::experimental::filesystem.
  RELEASE_ASSERT(::system(("rm -rf " + path).c_str()) == 0, "");
#endif
}

absl::optional<std::string> TestEnvironment::getOptionalEnvVar(const std::string& var) {
  const char* path = std::getenv(var.c_str());
  if (path == nullptr) {
    return {};
  }
  return std::string(path);
}

std::string TestEnvironment::getCheckedEnvVar(const std::string& var) {
  auto optional = getOptionalEnvVar(var);
  RELEASE_ASSERT(optional.has_value(), "");
  return optional.value();
}

void TestEnvironment::initializeOptions(int argc, char** argv) {
  argc_ = argc;
  argv_ = argv;
}

bool TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion type) {
  const char* value = std::getenv("ENVOY_IP_TEST_VERSIONS");
  std::string option(value ? value : "");
  if (option.empty()) {
    return true;
  }
  if ((type == Network::Address::IpVersion::v4 && option == "v6only") ||
      (type == Network::Address::IpVersion::v6 && option == "v4only")) {
    return false;
  }
  return true;
}

std::vector<Network::Address::IpVersion> TestEnvironment::getIpVersionsForTest() {
  std::vector<Network::Address::IpVersion> parameters;
  for (auto version : {Network::Address::IpVersion::v4, Network::Address::IpVersion::v6}) {
    if (TestEnvironment::shouldRunTestForIpVersion(version)) {
      parameters.push_back(version);
      if (!Network::Test::supportsIpVersion(version)) {
        ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::testing), warn,
                            "Testing with IP{} addresses may not be supported on this machine. If "
                            "testing fails, set the environment variable ENVOY_IP_TEST_VERSIONS.",
                            Network::Test::addressVersionAsString(version));
      }
    }
  }
  return parameters;
}

Server::Options& TestEnvironment::getOptions() {
  static OptionsImpl* options = new OptionsImpl(
      argc_, argv_, [](uint64_t, uint64_t, bool) { return "1"; }, spdlog::level::err);
  return *options;
}

const std::string& TestEnvironment::temporaryDirectory() {
  CONSTRUCT_ON_FIRST_USE(std::string, getTemporaryDirectory());
}

const std::string& TestEnvironment::runfilesDirectory() {
  CONSTRUCT_ON_FIRST_USE(std::string, getCheckedEnvVar("TEST_RUNDIR"));
}

const std::string TestEnvironment::unixDomainSocketDirectory() {
  CONSTRUCT_ON_FIRST_USE(std::string, getOrCreateUnixDomainSocketDirectory());
}

std::string TestEnvironment::substitute(const std::string& str,
                                        Network::Address::IpVersion version) {
  const std::unordered_map<std::string, std::string> path_map = {
      {"test_tmpdir", TestEnvironment::temporaryDirectory()},
      {"test_udsdir", TestEnvironment::unixDomainSocketDirectory()},
      {"test_rundir", TestEnvironment::runfilesDirectory()},
  };
  std::string out_json_string = str;
  for (auto it : path_map) {
    const std::regex port_regex("\\{\\{ " + it.first + " \\}\\}");
    out_json_string = std::regex_replace(out_json_string, port_regex, it.second);
  }

  // Substitute IP loopback addresses.
  const std::regex loopback_address_regex("\\{\\{ ip_loopback_address \\}\\}");
  out_json_string = std::regex_replace(out_json_string, loopback_address_regex,
                                       Network::Test::getLoopbackAddressUrlString(version));
  const std::regex ntop_loopback_address_regex("\\{\\{ ntop_ip_loopback_address \\}\\}");
  out_json_string = std::regex_replace(out_json_string, ntop_loopback_address_regex,
                                       Network::Test::getLoopbackAddressString(version));

  // Substitute IP any addresses.
  const std::regex any_address_regex("\\{\\{ ip_any_address \\}\\}");
  out_json_string = std::regex_replace(out_json_string, any_address_regex,
                                       Network::Test::getAnyAddressString(version));

  // Substitute dns lookup family.
  const std::regex lookup_family_regex("\\{\\{ dns_lookup_family \\}\\}");
  switch (version) {
  case Network::Address::IpVersion::v4:
    out_json_string = std::regex_replace(out_json_string, lookup_family_regex, "v4_only");
    break;
  case Network::Address::IpVersion::v6:
    out_json_string = std::regex_replace(out_json_string, lookup_family_regex, "v6_only");
    break;
  }

  return out_json_string;
}

std::string TestEnvironment::temporaryFileSubstitute(const std::string& path,
                                                     const PortMap& port_map,
                                                     Network::Address::IpVersion version) {
  return temporaryFileSubstitute(path, ParamMap(), port_map, version);
}

std::string TestEnvironment::readFileToStringForTest(const std::string& filename) {
  std::ifstream file(filename);
  if (file.fail()) {
    std::cerr << "failed to open: " << filename << std::endl;
    RELEASE_ASSERT(false, "");
  }

  std::stringstream file_string_stream;
  file_string_stream << file.rdbuf();
  return file_string_stream.str();
}

std::string TestEnvironment::temporaryFileSubstitute(const std::string& path,
                                                     const ParamMap& param_map,
                                                     const PortMap& port_map,
                                                     Network::Address::IpVersion version) {
  // Load the entire file as a string, regex replace one at a time and write it back out. Proper
  // templating might be better one day, but this works for now.
  const std::string json_path = TestEnvironment::runfilesPath(path);
  std::string out_json_string = readFileToStringForTest(json_path);

  // Substitute params.
  for (auto it : param_map) {
    const std::regex param_regex("\\{\\{ " + it.first + " \\}\\}");
    out_json_string = std::regex_replace(out_json_string, param_regex, it.second);
  }

  // Substitute ports.
  for (auto it : port_map) {
    const std::regex port_regex("\\{\\{ " + it.first + " \\}\\}");
    out_json_string = std::regex_replace(out_json_string, port_regex, std::to_string(it.second));
  }

  // Substitute paths and other common things.
  out_json_string = substitute(out_json_string, version);

  const std::string extension = absl::EndsWith(path, ".yaml") ? ".yaml" : ".json";
  const std::string out_json_path =
      TestEnvironment::temporaryPath(path + ".with.ports" + extension);
  createParentPath(out_json_path);
  {
    std::ofstream out_json_file(out_json_path);
    out_json_file << out_json_string;
  }
  return out_json_path;
}

Json::ObjectSharedPtr TestEnvironment::jsonLoadFromString(const std::string& json,
                                                          Network::Address::IpVersion version) {
  return Json::Factory::loadFromString(substitute(json, version));
}

void TestEnvironment::exec(const std::vector<std::string>& args) {
  std::stringstream cmd;
  // Symlinked args[0] can confuse Python when importing module relative, so we let Python know
  // where it can find its module relative files.
  cmd << "PYTHONPATH=$(dirname " << args[0] << ") ";
  for (auto& arg : args) {
    cmd << arg << " ";
  }
  if (::system(cmd.str().c_str()) != 0) {
    std::cerr << "Failed " << cmd.str() << "\n";
    RELEASE_ASSERT(false, "");
  }
}

std::string TestEnvironment::writeStringToFileForTest(const std::string& filename,
                                                      const std::string& contents) {
  const std::string out_path = TestEnvironment::temporaryPath(filename);
  createParentPath(out_path);
  unlink(out_path.c_str());
  {
    std::ofstream out_file(out_path);
    RELEASE_ASSERT(!out_file.fail(), "");
    out_file << contents;
  }
  return out_path;
}

void TestEnvironment::setEnvVar(const std::string& name, const std::string& value, int overwrite) {
#ifdef WIN32
  if (!overwrite) {
    size_t requiredSize;
    const int rc = ::getenv_s(&requiredSize, NULL, 0, name.c_str());
    ASSERT_EQ(rc, 0);
    if (requiredSize != 0) {
      return;
    }
  }
  const int rc = ::_putenv_s(name.c_str(), value.c_str());
  ASSERT_EQ(0, rc);
#else
  const int rc = ::setenv(name.c_str(), value.c_str(), overwrite);
  ASSERT_EQ(rc, 0);
#endif
}

} // namespace Envoy
