#ifndef SOURCE_COMMON_COMMON_MATCHING_URL_TEMPLATE_MATCHING_H
#define SOURCE_COMMON_COMMON_MATCHING_URL_TEMPLATE_MATCHING_H

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "source/common/common/matching/url_template_matching_internal.h"
#include "envoy/config/route/v3/route_components.pb.h"

namespace matching {

enum class RewriteStringKind { kVariable, kLiteral };

struct RewritePatternSegment {
  RewritePatternSegment(absl::string_view str, RewriteStringKind kind) : str(str), kind(kind) {}
  absl::string_view str;
  RewriteStringKind kind;
};

// Returns the regex pattern that is equivalent to the given url_pattern.
// Used in the config pipeline to translate user given url pattern to
// the safe regex Envoy can understand. Strips away any variable captures.
absl::StatusOr<std::string> ConvertURLPatternSyntaxToRegex(absl::string_view url_pattern);

// Helper function that parses the pattern and breaks it down to either
// literals or variable names. To be used by ParseRewritePattern().
// Exposed here so that the validator for the rewrite pattern can also
// use it.
absl::StatusOr<std::vector<RewritePatternSegment>>
ParseRewritePatternHelper(absl::string_view pattern);

// Returns the parsed Url rewrite pattern to be used by
// RewriteURLTemplatePattern()in the BdnFilter. |capture_regex| should
// be the regex generated by ConvertURLPatternSyntaxToRegex().
absl::StatusOr<envoy::config::route::v3::RouteUrlRewritePattern>
ParseRewritePattern(absl::string_view pattern, absl::string_view capture_regex);

// Returns the rewritten URL path based on the given parsed rewrite pattern.
// Used in the BdnFilter for template-based URL rewrite.
absl::StatusOr<std::string>
RewriteURLTemplatePattern(absl::string_view url, absl::string_view capture_regex,
                          const envoy::config::route::v3::RouteUrlRewritePattern& rewrite_pattern);

} // namespace matching

#endif // SOURCE_COMMON_COMMON_MATCHING_URL_TEMPLATE_MATCHING_H