#pragma once

#include <string>

#include "envoy/router/path_match.h"

#include "source/extensions/path/pattern_template_lib/pattern_template_internal.h"
#include "source/extensions/path/pattern_template_lib/proto/pattern_template_rewrite_segments.pb.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {

enum class RewriteStringKind { KVariable, KLiteral };

struct RewritePatternSegment {
  RewritePatternSegment(absl::string_view segment_value, RewriteStringKind kind)
      : segment_value(segment_value), kind(kind) {}
  absl::string_view segment_value;
  RewriteStringKind kind;
};

// Returns the regex pattern that is equivalent to the given url_pattern.
// Used in the config pipeline to translate user given url pattern to
// the safe regex Envoy can understand. Strips away any variable captures.
absl::StatusOr<std::string> convertURLPatternSyntaxToRegex(absl::string_view url_pattern);

// Helper function that parses the pattern and breaks it down to either
// literals or variable names. To be used by ParseRewritePattern().
// Exposed here so that the validator for the rewrite pattern can also
// use it.
absl::StatusOr<std::vector<RewritePatternSegment>>
parseRewritePatternHelper(absl::string_view pattern);

// Returns the parsed Url rewrite pattern to be used by
// RewriteURLTemplatePattern()  |capture_regex| should
// be the regex generated by ConvertURLPatternSyntaxToRegex().
absl::StatusOr<envoy::extensions::pattern_template::PatternTemplateRewriteSegments>
parseRewritePattern(absl::string_view pattern, absl::string_view capture_regex);

// Returns if provided rewrite pattern is valid
absl::Status isValidPathTemplateRewritePattern(const std::string& path_template_rewrite);

// Returns if path_template and rewrite_template have valid variables
absl::Status isValidSharedVariableSet(const std::string& path_template_rewrite,
                                      const std::string& capture_regex);

absl::Status isValidMatchPattern(const std::string match_pattern);

absl::StatusOr<std::string> rewriteURLTemplatePattern(
    absl::string_view url, absl::string_view capture_regex,
    const envoy::extensions::pattern_template::PatternTemplateRewriteSegments& rewrite_pattern);

} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
