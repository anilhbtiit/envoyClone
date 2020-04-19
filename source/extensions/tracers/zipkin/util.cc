#include "extensions/tracers/zipkin/util.h"

#include <chrono>
#include <random>
#include <regex>

#include "common/common/hex.h"
#include "common/common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

uint64_t Util::generateRandom64(TimeSource& time_source) {
  uint64_t seed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      time_source.systemTime().time_since_epoch())
                      .count();
  std::mt19937_64 rand_64(seed);
  return rand_64();
}

ProtobufWkt::Value Util::uint64Value(uint64_t value, Replacements& replacements) {
  const std::string string_value = std::to_string(value);
  replacements.push_back({absl::StrCat("\"", string_value, "\""), string_value});
  return ValueUtil::stringValue(string_value);
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
