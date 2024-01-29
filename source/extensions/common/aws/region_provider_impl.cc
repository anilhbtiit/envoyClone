#include "source/extensions/common/aws/region_provider_impl.h"

#include "source/extensions/common/aws/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

static const char AWS_REGION[] = "AWS_REGION";
static const char AWS_DEFAULT_REGION[] = "AWS_DEFAULT_REGION";
constexpr char REGION[] = "REGION";

absl::optional<std::string> EnvironmentRegionProvider::getRegion() {
  std::string region;

  // Search for the region in environment variables AWS_REGION and AWS_DEFAULT_REGION
  region = Utility::getEnvironmentVariableOrDefault(AWS_REGION, "");
  if (region.empty()) {
    region = Utility::getEnvironmentVariableOrDefault(AWS_DEFAULT_REGION, "");
    if (region.empty()) {
      return absl::nullopt;
    }
  }
  ENVOY_LOG_MISC(debug, "Region string retrieved: {}");
  return region;
}

absl::optional<std::string> AWSCredentialsFileRegionProvider::getRegion() {
  absl::flat_hash_map<std::string, std::string> elements = {{REGION, ""}};
  absl::flat_hash_map<std::string, std::string>::iterator it;

  // Search for the region in the credentials file

  if (!Utility::resolveProfileElements(Utility::getCredentialFilePath(),
                                       Utility::getCredentialProfileName(), elements)) {
    return absl::nullopt;
  }
  it = elements.find(REGION);
  if (it == elements.end()) {
    return absl::nullopt;
  }

  ENVOY_LOG_MISC(debug, "Region string retrieved: {}");
  return it->second;
}

absl::optional<std::string> AWSConfigFileRegionProvider::getRegion() {
  absl::flat_hash_map<std::string, std::string> elements = {{REGION, ""}};
  absl::flat_hash_map<std::string, std::string>::iterator it;

  // Search for the region in the config file

  if (!Utility::resolveProfileElements(Utility::getConfigFilePath(),
                                       Utility::getConfigProfileName(), elements)) {
    return absl::nullopt;
  }

  it = elements.find(REGION);
  if (it == elements.end()) {
    return absl::nullopt;
  }

  ENVOY_LOG_MISC(debug, "Region string retrieved: {}");
  return it->second;
}

// Region provider chain. This allows retrieving region information from the following locations (in
// order):
// 1. The envoy configuration, in the region parameter
// 2. The envoy environment, in AWS_REGION then AWS_DEFAULT_REGION
// 3. In the credentials file $HOME/.aws/credentials (or location from
//    AWS_SHARED_CREDENTIALS_FILE/AWS_DEFAULT_SHARED_CREDENTIALS_FILE), under profile section
//    specified by AWS_PROFILE
// 4. In the config file $HOME/.aws/config (or location from AWS_CONFIG_FILE), under profile section
// specified by AWS_PROFILE
//
// Credentials and profile format can be found here:
// https://docs.aws.amazon.com/cli/latest/userguide/cli-configure-files.html
//
RegionProviderChain::RegionProviderChain() {
  add(createEnvironmentRegionProvider());
  add(createAWSCredentialsFileRegionProvider());
  add(createAWSConfigFileRegionProvider());
}

absl::optional<std::string> RegionProviderChain::getRegion() {
  for (auto& provider : providers_) {
    const auto region = provider->getRegion();
    if (region.has_value()) {
      return region;
    }
  }
  return absl::nullopt;
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
