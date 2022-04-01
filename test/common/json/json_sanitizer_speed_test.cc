#include "source/common/json/json_internal.h"
#include "source/common/json/json_sanitizer.h"
#include "source/common/protobuf/utility.h"

#include "benchmark/benchmark.h"

// NOLINT(namespace-envoy)

constexpr absl::string_view pass_through_encoding = "Now is the time for all good men";
constexpr absl::string_view escaped_encoding = "Now <is the \"time\"> for all good men";

const Envoy::Json::JsonSanitizer& staticSanitizer() {
  CONSTRUCT_ON_FIRST_USE(Envoy::Json::JsonSanitizer);
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_ProtoEncoderNoEscape(benchmark::State& state) {
  const std::string str = std::string(pass_through_encoding);

  for (auto _ : state) { // NOLINT
    Envoy::MessageUtil::getJsonStringFromMessageOrDie(Envoy::ValueUtil::stringValue(str), false,
                                                      true);
  }
}
BENCHMARK(BM_ProtoEncoderNoEscape);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_JsonSanitizerNoEscape(benchmark::State& state) {
  std::string buffer;
  Envoy::Json::JsonSanitizer sanitizer;

  for (auto _ : state) { // NOLINT
    sanitizer.sanitize(buffer, pass_through_encoding);
  }
}
BENCHMARK(BM_JsonSanitizerNoEscape);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_NlohmannNoEscape(benchmark::State& state) {
  for (auto _ : state) { // NOLINT
    Envoy::Json::Nlohmann::Factory::serialize(pass_through_encoding);
  }
}
BENCHMARK(BM_NlohmannNoEscape);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StaticJsonSanitizerNoEscape(benchmark::State& state) {
  std::string buffer;

  for (auto _ : state) { // NOLINT
    staticSanitizer().sanitize(buffer, pass_through_encoding);
  }
}
BENCHMARK(BM_StaticJsonSanitizerNoEscape);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_ProtoEncoderWithEscape(benchmark::State& state) {
  const std::string str = std::string(escaped_encoding);

  for (auto _ : state) { // NOLINT
    Envoy::MessageUtil::getJsonStringFromMessageOrDie(Envoy::ValueUtil::stringValue(str), false,
                                                      true);
  }
}
BENCHMARK(BM_ProtoEncoderWithEscape);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_NlohmannWithEscape(benchmark::State& state) {
  const std::string str = std::string(escaped_encoding);

  for (auto _ : state) { // NOLINT
    Envoy::Json::Nlohmann::Factory::serialize(str);
  }
}
BENCHMARK(BM_NlohmannWithEscape);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_JsonSanitizerWithEscape(benchmark::State& state) {
  Envoy::Json::JsonSanitizer sanitizer;
  std::string buffer;

  for (auto _ : state) { // NOLINT
    sanitizer.sanitize(buffer, escaped_encoding);
  }
}
BENCHMARK(BM_JsonSanitizerWithEscape);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StaticJsonSanitizerWithEscape(benchmark::State& state) {
  std::string buffer;

  for (auto _ : state) { // NOLINT
    staticSanitizer().sanitize(buffer, escaped_encoding);
  }
}
BENCHMARK(BM_StaticJsonSanitizerWithEscape);
