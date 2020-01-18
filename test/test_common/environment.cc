#include "test/test_common/environment.h"

// TODO(asraa): Remove <experimental/filesystem> and rely only on <filesystem> when Envoy requires
// Clang >= 9.
#if defined(_LIBCPP_VERSION) && !defined(__APPLE__)
#include <filesystem>
#elif defined __has_include
#if __has_include(<experimental/filesystem>) && !defined(__APPLE__)
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
#include "envoy/common/platform.h"

#include "server/options_impl.h"

#include "test/test_common/network_utility.h"

#include "absl/strings/match.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

using bazel::tools::cpp::runfiles::Runfiles;

namespace Envoy {
namespace {

std::string makeTempDir(std::string basename_template) {
#ifdef WIN32
  std::string name_template = "c:\\Windows\\TEMP\\" + basename_template;
  char* dirname = ::_mktemp(&name_template[0]);
  RELEASE_ASSERT(dirname != nullptr, fmt::format("failed to create tempdir from template: {} {}",
                                                 name_template, strerror(errno)));
#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION >= 9000 && !defined(__APPLE__)
  std::__fs::filesystem::create_directories(dirname);
#elif defined __cpp_lib_experimental_filesystem && !defined(__APPLE__)
  std::experimental::filesystem::create_directories(dirname);
#endif
#else
  std::string name_template = "/tmp/" + basename_template;
  char* dirname = ::mkdtemp(&name_template[0]);
  RELEASE_ASSERT(dirname != nullptr, fmt::format("failed to create tempdir from template: {} {}",
                                                 name_template, strerror(errno)));
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
  return makeTempDir("envoy_test_uds.XXXXXX");
}

std::string getTemporaryDirectory() {
  if (std::getenv("TEST_TMPDIR")) {
    return TestEnvironment::getCheckedEnvVar("TEST_TMPDIR");
  }
  if (std::getenv("TMPDIR")) {
    return TestEnvironment::getCheckedEnvVar("TMPDIR");
  }
  return makeTempDir("envoy_test_tmp.XXXXXX");
}

// Allow initializeOptions() to remember CLI args for getOptions().
int argc_;
char** argv_;

} // namespace

void TestEnvironment::createPath(const std::string& path) {
#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION >= 9000 && !defined(__APPLE__)
  // We don't want to rely on mkdir etc. if we can avoid it, since it might not
  // exist in some environments such as ClusterFuzz.
  std::__fs::filesystem::create_directories(std::__fs::filesystem::path(path));
#elif defined __cpp_lib_experimental_filesystem
  std::experimental::filesystem::create_directories(std::experimental::filesystem::path(path));
#else
  // No support on this system for std::filesystem or std::experimental::filesystem.
  RELEASE_ASSERT(::system(("mkdir -p " + path).c_str()) == 0, "");
#endif
}

void TestEnvironment::createParentPath(const std::string& path) {
#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION >= 9000 && !defined(__APPLE__)
  // We don't want to rely on mkdir etc. if we can avoid it, since it might not
  // exist in some environments such as ClusterFuzz.
  std::__fs::filesystem::create_directories(std::__fs::filesystem::path(path).parent_path());
#elif defined __cpp_lib_experimental_filesystem && !defined(__APPLE__)
  std::experimental::filesystem::create_directories(
      std::experimental::filesystem::path(path).parent_path());
#else
  // No support on this system for std::filesystem or std::experimental::filesystem.
  RELEASE_ASSERT(::system(("mkdir -p $(dirname " + path + ")").c_str()) == 0, "");
#endif
}

void TestEnvironment::removePath(const std::string& path) {
  RELEASE_ASSERT(absl::StartsWith(path, TestEnvironment::temporaryDirectory()), "");
#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION >= 9000 && !defined(__APPLE__)
  // We don't want to rely on mkdir etc. if we can avoid it, since it might not
  // exist in some environments such as ClusterFuzz.
  if (!std::__fs::filesystem::exists(path)) {
    return;
  }
  std::__fs::filesystem::remove_all(std::__fs::filesystem::path(path));
#elif defined __cpp_lib_experimental_filesystem && !defined(__APPLE__)
  if (!std::experimental::filesystem::exists(path)) {
    return;
  }
  std::experimental::filesystem::remove_all(std::experimental::filesystem::path(path));
#else
  // No support on this system for std::filesystem or std::experimental::filesystem.
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
  RELEASE_ASSERT(optional.has_value(), var);
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
      argc_, argv_, [](bool) { return "1"; }, spdlog::level::err);
  return *options;
}

const std::string& TestEnvironment::temporaryDirectory() {
  CONSTRUCT_ON_FIRST_USE(std::string, getTemporaryDirectory());
}

std::string TestEnvironment::runfilesDirectory(const std::string& workspace) {
  RELEASE_ASSERT(runfiles_ != nullptr, "");
  return runfiles_->Rlocation(workspace);
}

std::string TestEnvironment::runfilesPath(const std::string& path, const std::string& workspace) {
  RELEASE_ASSERT(runfiles_ != nullptr, "");
  return runfiles_->Rlocation(absl::StrCat(workspace, "/", path));
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
  for (const auto& it : path_map) {
    const std::regex port_regex("\\{\\{ " + it.first + " \\}\\}");
    out_json_string = std::regex_replace(out_json_string, port_regex, it.second);
  }

  // Substitute IP loopback addresses.
  const std::regex loopback_address_regex(R"(\{\{ ip_loopback_address \}\})");
  out_json_string = std::regex_replace(out_json_string, loopback_address_regex,
                                       Network::Test::getLoopbackAddressString(version));
  const std::regex ntop_loopback_address_regex(R"(\{\{ ntop_ip_loopback_address \}\})");
  out_json_string = std::regex_replace(out_json_string, ntop_loopback_address_regex,
                                       Network::Test::getLoopbackAddressString(version));

  // Substitute IP any addresses.
  const std::regex any_address_regex(R"(\{\{ ip_any_address \}\})");
  out_json_string = std::regex_replace(out_json_string, any_address_regex,
                                       Network::Test::getAnyAddressString(version));

  // Substitute dns lookup family.
  const std::regex lookup_family_regex(R"(\{\{ dns_lookup_family \}\})");
  switch (version) {
  case Network::Address::IpVersion::v4:
    out_json_string = std::regex_replace(out_json_string, lookup_family_regex, "v4_only");
    break;
  case Network::Address::IpVersion::v6:
    out_json_string = std::regex_replace(out_json_string, lookup_family_regex, "v6_only");
    break;
  }

  // Substitute socket options arguments.
  const std::regex sol_socket_regex(R"(\{\{ sol_socket \}\})");
  out_json_string =
      std::regex_replace(out_json_string, sol_socket_regex, std::to_string(SOL_SOCKET));
  const std::regex so_reuseport_regex(R"(\{\{ so_reuseport \}\})");
  out_json_string =
      std::regex_replace(out_json_string, so_reuseport_regex, std::to_string(SO_REUSEPORT));

  return out_json_string;
}

std::string TestEnvironment::temporaryFileSubstitute(const std::string& path,
                                                     const PortMap& port_map,
                                                     Network::Address::IpVersion version) {
  return temporaryFileSubstitute(path, ParamMap(), port_map, version);
}

std::string TestEnvironment::readFileToStringForTest(const std::string& filename,
                                                     bool require_existence) {
  std::ifstream file(filename, std::ios::binary);
  if (file.fail()) {
    if (!require_existence) {
      return "";
    }
    RELEASE_ASSERT(false, absl::StrCat("failed to open: ", filename));
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
  for (const auto& it : param_map) {
    const std::regex param_regex("\\{\\{ " + it.first + " \\}\\}");
    out_json_string = std::regex_replace(out_json_string, param_regex, it.second);
  }

  // Substitute ports.
  for (const auto& it : port_map) {
    const std::regex port_regex("\\{\\{ " + it.first + " \\}\\}");
    out_json_string = std::regex_replace(out_json_string, port_regex, std::to_string(it.second));
  }

  // Substitute paths and other common things.
  out_json_string = substitute(out_json_string, version);

  const std::string extension = absl::EndsWith(path, ".yaml")
                                    ? ".yaml"
                                    : absl::EndsWith(path, ".pb_text") ? ".pb_text" : ".json";
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
  cmd << "bash -c \"PYTHONPATH=$(dirname " << args[0] << ") ";
  for (auto& arg : args) {
    cmd << arg << " ";
  }
  cmd << "\"";
  if (::system(cmd.str().c_str()) != 0) {
    std::cerr << "Failed " << cmd.str() << "\n";
    RELEASE_ASSERT(false, "");
  }
}

std::string TestEnvironment::writeStringToFileForTest(const std::string& filename,
                                                      const std::string& contents,
                                                      bool fully_qualified_path) {
  const std::string out_path =
      fully_qualified_path ? filename : TestEnvironment::temporaryPath(filename);
  createParentPath(out_path);
  unlink(out_path.c_str());
  {
    std::ofstream out_file(out_path, std::ios_base::binary);
    RELEASE_ASSERT(!out_file.fail(), "");
    out_file << contents;
  }
  return out_path;
}

void TestEnvironment::setEnvVar(const std::string& name, const std::string& value, int overwrite) {
#ifdef WIN32
  if (!overwrite) {
    size_t requiredSize;
    const int rc = ::getenv_s(&requiredSize, nullptr, 0, name.c_str());
    ASSERT_EQ(0, rc);
    if (requiredSize != 0) {
      return;
    }
  }
  const int rc = ::_putenv_s(name.c_str(), value.c_str());
  ASSERT_EQ(0, rc);
#else
  const int rc = ::setenv(name.c_str(), value.c_str(), overwrite);
  ASSERT_EQ(0, rc);
#endif
}
void TestEnvironment::unsetEnvVar(const std::string& name) {
#ifdef WIN32
  const int rc = ::_putenv_s(name.c_str(), "");
  ASSERT_EQ(0, rc);
#else
  const int rc = ::unsetenv(name.c_str());
  ASSERT_EQ(0, rc);
#endif
}

void TestEnvironment::setRunfiles(Runfiles* runfiles) { runfiles_ = runfiles; }

Runfiles* TestEnvironment::runfiles_{};

} // namespace Envoy
