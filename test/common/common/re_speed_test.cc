// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include <regex>

#include "common/common/assert.h"

#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"

#include "re2/re2.h"

static const char* cluster_inputs[] = {
  "cluster.no_trailing_dot",
  "cluster.match.",
  "cluster.match.normal",
  "cluster.match.and.a.whole.lot.of.things.coming.after.the.matches.really.too.much.stuff",
};

static const char cluster_re_pattern[] = "^cluster\\.((.*?)\\.)";
static const char cluster_re_alt_pattern[] = "^cluster\\.([^\\.]+)\\..*";

static void BM_StdRegex(benchmark::State& state) {
  std::regex re(cluster_re_pattern);
  uint32_t passes = 0;
  std::vector<std::string> inputs;
  for (const char* cluster_input : cluster_inputs) {
    inputs.push_back(cluster_input);
  }

  for (auto _ : state) {
    for (const std::string& cluster_input : inputs) {
      std::smatch match;
      if (std::regex_search(cluster_input, match, re)) {
        ASSERT(match.size() >= 3);
        ASSERT(match[1] == "match.");
        ASSERT(match[2] == "match");
        ++passes;
      }
    }
  }
  RELEASE_ASSERT(passes > 0, "");
}
BENCHMARK(BM_StdRegex);

static void BM_StdRegexStringView(benchmark::State& state) {
  std::regex re(cluster_re_pattern);
  std::vector<absl::string_view> inputs;
  for (const char* cluster_input : cluster_inputs) {
    inputs.push_back(cluster_input);
  }
  uint32_t passes = 0;
  for (auto _ : state) {
    for (absl::string_view cluster_input : inputs) {
      std::match_results<absl::string_view::iterator> smatch;
      if (std::regex_search(cluster_input.begin(), cluster_input.end(), smatch, re)) {
        ASSERT(smatch.size() >= 3);
        ASSERT(smatch[1] == "match.");
        ASSERT(smatch[2] == "match");
        ++passes;
      }
    }
  }
  RELEASE_ASSERT(passes > 0, "");
}
BENCHMARK(BM_StdRegexStringView);

static void BM_StdRegexStringViewAltPattern(benchmark::State& state) {
  std::regex re(cluster_re_alt_pattern);
  std::vector<absl::string_view> inputs;
  for (const char* cluster_input : cluster_inputs) {
    inputs.push_back(cluster_input);
  }
  uint32_t passes = 0;
  for (auto _ : state) {
    for (absl::string_view cluster_input : inputs) {
      std::match_results<absl::string_view::iterator> smatch;
      if (std::regex_search(cluster_input.begin(), cluster_input.end(), smatch, re)) {
        ASSERT(smatch.size() >= 2);
        ASSERT(smatch[1] == "match");
        ++passes;
      }
    }
  }
  RELEASE_ASSERT(passes > 0, "");
}
BENCHMARK(BM_StdRegexStringViewAltPattern);

static void BM_RE2(benchmark::State& state) {
  re2::RE2 re(cluster_re_pattern);
  uint32_t passes = 0;
  for (auto _ : state) {
    for (const char* cluster_input : cluster_inputs) {
      re2::StringPiece match1, match2;
      if (re2::RE2::PartialMatch(cluster_input, re, &match1, &match2)) {
        ASSERT(match1 == "match.");
        ASSERT(match2 == "match");
        ++passes;
      }
    }
  }
  RELEASE_ASSERT(passes > 0, "");
}
BENCHMARK(BM_RE2);

static void BM_RE2_AltPattern(benchmark::State& state) {
  re2::RE2 re(cluster_re_alt_pattern);
  uint32_t passes = 0;
  for (auto _ : state) {
    for (const char* cluster_input : cluster_inputs) {
      re2::StringPiece match;
      if (re2::RE2::PartialMatch(cluster_input, re, &match)) {
        ASSERT(match == "match");
        ++passes;
      }
    }
  }
  RELEASE_ASSERT(passes > 0, "");
}
BENCHMARK(BM_RE2_AltPattern);

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
