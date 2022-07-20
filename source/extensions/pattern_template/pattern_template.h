#ifndef SOURCE_EXTENSIONS_PATTERN_TEMPLATE_PATTERN_TEMPLATE_H
#define SOURCE_EXTENSIONS_PATTERN_TEMPLATE_PATTERN_TEMPLATE_H

#include <string>

#include "envoy/extensions/pattern_template/rewrite/v3/pattern_template_rewrite.pb.h"
#include "envoy/router/pattern_template.h"

#include "source/extensions/pattern_template/pattern_template_internal.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {

enum class RewriteStringKind { kVariable, kLiteral };

struct RewritePatternSegment {
  RewritePatternSegment(absl::string_view str, RewriteStringKind kind) : str(str), kind(kind) {}
  absl::string_view str;
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
absl::StatusOr<envoy::extensions::pattern_template::rewrite::v3::PatternTemplateRewrite>
parseRewritePattern(absl::string_view pattern, absl::string_view capture_regex);

// Returns if provided rewrite pattern is valid
absl::Status isValidPathTemplateRewritePattern(const std::string& path_template_rewrite);

// Returns if path_template and rewrite_template have valid variables
absl::Status isValidSharedVariableSet(const std::string& path_template_rewrite,
                                      std::string& capture_regex);

absl::Status isValidMatchPattern(std::string match_pattern);

} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy

#endif // SOURCE_EXTENSIONS_PATTERN_TEMPLATE_PATTERN_TEMPLATE_H
