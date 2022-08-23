#include "source/extensions/path/uri_template_lib/uri_template.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "source/common/http/path_utility.h"
#include "source/extensions/path/uri_template_lib/proto/rewrite_segments.pb.h"
#include "source/extensions/path/uri_template_lib/uri_template_internal.h"

#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "re2/re2.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {

using Internal::ParsedPathPattern;

#ifndef SWIG
// Silence warnings about missing initializers for members of LazyRE2.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

absl::StatusOr<std::string> convertPathPatternSyntaxToRegex(absl::string_view path_pattern) {
  absl::StatusOr<ParsedPathPattern> status = Internal::parsePathPatternSyntax(path_pattern);
  if (!status.ok()) {
    return status.status();
  }
  return Internal::toRegexPattern(*status);
}

absl::StatusOr<std::vector<ParsedSegment>> parseRewritePattern(absl::string_view path_pattern) {
  std::vector<ParsedSegment> result;

  // The pattern should start with a '/' and thus the first segment should
  // always be a literal.
  if (path_pattern.empty() || path_pattern[0] != '/') {
    return absl::InvalidArgumentError("Invalid rewrite variable placement");
  }

  // Don't allow contiguous '/' patterns.
  static const LazyRE2 invalid_regex = {"^.*//.*$"};
  if (RE2::FullMatch(Internal::toStringPiece(path_pattern), *invalid_regex)) {
    return absl::InvalidArgumentError("Invalid rewrite literal");
  }

  while (!path_pattern.empty()) {
    std::vector<absl::string_view> segments1 =
        absl::StrSplit(path_pattern, absl::MaxSplits('{', 1));
    if (!segments1[0].empty()) {
      if (!Internal::isValidRewriteLiteral(segments1[0])) {
        return absl::InvalidArgumentError("Invalid rewrite literal pattern");
      }
      result.emplace_back(segments1[0], RewriteStringKind::Literal);
    }

    if (segments1.size() < 2) {
      // No more variable replacement, done.
      break;
    }

    std::vector<absl::string_view> segments2 =
        absl::StrSplit(segments1[1], absl::MaxSplits('}', 1));
    if (segments2.size() < 2) {
      return absl::InvalidArgumentError("Unmatched variable bracket");
    }
    path_pattern = segments2[1];

    if (!Internal::isValidVariableName(segments2[0])) {
      return absl::InvalidArgumentError("Invalid variable name");
    }
    result.emplace_back(segments2[0], RewriteStringKind::Variable);
  }
  return result;
}

absl::StatusOr<envoy::extensions::uri_template::RewriteSegments>
parseRewritePattern(absl::string_view pattern, absl::string_view capture_regex) {
  envoy::extensions::uri_template::RewriteSegments parsed_pattern;
  RE2 regex = RE2(Internal::toStringPiece(capture_regex));
  if (!regex.ok()) {
    return absl::InternalError(regex.error());
  }

  absl::StatusOr<std::vector<ParsedSegment>> status = parseRewritePattern(pattern);
  if (!status.ok()) {
    return status.status();
  }
  std::vector<ParsedSegment> processed_pattern = *std::move(status);

  const std::map<std::string, int>& capture_index_map = regex.NamedCapturingGroups();

  for (const auto& [str, kind] : processed_pattern) {
    switch (kind) {
    case RewriteStringKind::Literal:
      parsed_pattern.add_segments()->set_literal(std::string(str));
      break;
    case RewriteStringKind::Variable:
      auto it = capture_index_map.find(std::string(str));
      if (it == capture_index_map.end()) {
        return absl::InvalidArgumentError("Nonexisting variable name");
      }
      parsed_pattern.add_segments()->set_capture_index(it->second);
      break;
    }
  }

  return parsed_pattern;
}

absl::Status isValidMatchPattern(absl::string_view path_template_match) {
  return convertPathPatternSyntaxToRegex(path_template_match).status();
}

absl::Status isValidRewritePattern(absl::string_view path_template_rewrite) {
  return parseRewritePattern(path_template_rewrite).status();
}

absl::Status isValidSharedVariableSet(absl::string_view pattern, absl::string_view capture_regex) {
  absl::StatusOr<std::string> status = convertPathPatternSyntaxToRegex(capture_regex).value();
  if (!status.ok()) {
    return status.status();
  }
  return parseRewritePattern(pattern, *std::move(status)).status();
}

absl::StatusOr<std::string> rewritePathTemplatePattern(
    absl::string_view path, absl::string_view capture_regex,
    const envoy::extensions::uri_template::RewriteSegments& rewrite_pattern) {
  RE2 regex = RE2(Internal::toStringPiece(capture_regex));
  if (!regex.ok()) {
    return absl::InternalError(regex.error());
  }

  // First capture is the whole matched regex pattern.
  int capture_num = regex.NumberOfCapturingGroups() + 1;
  std::vector<re2::StringPiece> captures(capture_num);
  if (!regex.Match(Internal::toStringPiece(path), /*startpos=*/0,
                   /*endpos=*/path.size(), RE2::ANCHOR_BOTH, captures.data(), captures.size())) {
    return absl::InvalidArgumentError("Pattern does not match");
  }

  std::string rewritten_path;
  for (const envoy::extensions::uri_template::RewriteSegments::RewriteSegment& segment :
       rewrite_pattern.segments()) {
    if (segment.has_literal()) {
      absl::StrAppend(&rewritten_path, segment.literal());
    } else if (segment.has_capture_index()) {
      if (segment.capture_index() < 1 || segment.capture_index() >= capture_num) {
        return absl::InvalidArgumentError("Invalid variable index");
      }
      absl::StrAppend(&rewritten_path,
                      absl::string_view(captures[segment.capture_index()].as_string()));
    }
  }

  return rewritten_path;
}

} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
