#include "source/extensions/filters/http/ext_proc/mutation_utils.h"

#include "envoy/http/header_map.h"

#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using Filters::Common::MutationRules::Checker;
using Filters::Common::MutationRules::CheckOperation;
using Filters::Common::MutationRules::CheckResult;
using Http::Headers;
using Http::LowerCaseString;
using Stats::Counter;

using envoy::service::ext_proc::v3::BodyMutation;
using envoy::service::ext_proc::v3::HeaderMutation;

using HeaderAppendAction = envoy::config::core::v3::HeaderValueOption::HeaderAppendAction;
using HeaderValueOption = envoy::config::core::v3::HeaderValueOption;

bool MutationUtils::headerInMatcher(
    absl::string_view key, const std::vector<Matchers::StringMatcherPtr>& header_matchers) {
  return std::any_of(header_matchers.begin(), header_matchers.end(),
                     [&key](auto& matcher) { return matcher->match(key); });
}

bool MutationUtils::headerCanBeForwarded(
    absl::string_view key, const std::vector<Matchers::StringMatcherPtr>& allowed_headers,
    const std::vector<Matchers::StringMatcherPtr>& disallowed_headers) {
  // disallow list empty
  if (disallowed_headers.empty()) {
    if (allowed_headers.empty() || headerInMatcher(key, allowed_headers)) {
      return true;
    }
    return false;
  }

  // Now disallow list is set.
  if (headerInMatcher(key, disallowed_headers)) {
    return false;
  }
  if (allowed_headers.empty() || headerInMatcher(key, allowed_headers)) {
    return true;
  }
  return false;
}

void MutationUtils::headersToProto(
    const Http::HeaderMap& headers_in,
    const std::vector<Matchers::StringMatcherPtr>& allowed_headers,
    const std::vector<Matchers::StringMatcherPtr>& disallowed_headers,
    envoy::config::core::v3::HeaderMap& proto_out) {
  headers_in.iterate([&proto_out, &allowed_headers,
                      &disallowed_headers](const Http::HeaderEntry& e) -> Http::HeaderMap::Iterate {
    if (headerCanBeForwarded(e.key().getStringView(), allowed_headers, disallowed_headers)) {
      auto* new_header = proto_out.add_headers();
      new_header->set_key(std::string(e.key().getStringView()));
      // Setting up value or raw_value field based on the runtime flag.
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.send_header_raw_value")) {
        new_header->set_raw_value(std::string(e.value().getStringView()));
      } else {
        new_header->set_value(MessageUtil::sanitizeUtf8String(e.value().getStringView()));
      }
    }
    return Http::HeaderMap::Iterate::Continue;
  });
}

absl::Status MutationUtils::responseHeaderSizeCheck(const Http::HeaderMap& headers,
                                                    const HeaderMutation& mutation,
                                                    Counter& rejected_mutations) {
  const uint32_t remove_size = mutation.remove_headers().size();
  const uint32_t set_size = mutation.set_headers().size();
  const uint32_t max_request_headers_count = headers.maxHeadersCount();

  if (remove_size > max_request_headers_count || set_size > max_request_headers_count) {
    ENVOY_LOG(debug,
              "Header mutation remove header count {} or set header count {} exceed the "
              "max header count limit {}. Returning error.",
              remove_size, set_size, max_request_headers_count);
    rejected_mutations.inc();
    return absl::InvalidArgumentError(absl::StrCat(
        "Header mutation remove header count ", std::to_string(remove_size),
        " or set header count ", std::to_string(set_size), " exceed the HCM header countlimit ",
        std::to_string(max_request_headers_count)));
  }
  return absl::OkStatus();
}

absl::Status MutationUtils::headerMutationResultCheck(const Http::HeaderMap& headers,
                                                      Counter& rejected_mutations) {
  if (headers.byteSize() > headers.maxHeadersKb() * 1024 ||
      headers.size() > headers.maxHeadersCount()) {
    ENVOY_LOG(debug,
              "After mutation, the total header count {} or total header size {} bytes, exceed the "
              "count limit {} or the size limit {} kilobytes. Returning error.",
              headers.size(), headers.byteSize(), headers.maxHeadersCount(),
              headers.maxHeadersKb());
    rejected_mutations.inc();
    return absl::InvalidArgumentError(absl::StrCat(
        "Header mutation causes end result header count ", headers.size(), " or header size ",
        headers.byteSize(), " bytes, exceeding the count limit ", headers.maxHeadersCount(),
        " or the size limit ", headers.maxHeadersKb(), " kilobytes"));
  }
  return absl::OkStatus();
}

absl::Status MutationUtils::applyHeaderMutations(const HeaderMutation& mutation,
                                                 Http::HeaderMap& headers, bool replacing_message,
                                                 const Checker& checker,
                                                 Counter& rejected_mutations) {
  // Check whether the remove_headers or set_headers size exceed the HTTP connection manager limit.
  // Reject the mutation and return error status if either one does.
  const auto result = responseHeaderSizeCheck(headers, mutation, rejected_mutations);
  if (!result.ok()) {
    return result;
  }

  for (const auto& hdr : mutation.remove_headers()) {
    if (!Http::HeaderUtility::headerNameIsValid(hdr)) {
      ENVOY_LOG(debug, "remove_headers contain invalid character, may not be removed.");
      rejected_mutations.inc();
      return absl::InvalidArgumentError("Invalid character in remove_headers mutation.");
    }
    const LowerCaseString remove_header(hdr);
    switch (checker.check(CheckOperation::REMOVE, remove_header, "")) {
    case CheckResult::OK:
      ENVOY_LOG(trace, "Removing header {}", remove_header);
      headers.remove(remove_header);
      break;
    case CheckResult::IGNORE:
      ENVOY_LOG(debug, "Header {} may not be removed per rules", remove_header);
      rejected_mutations.inc();
      break;
    case CheckResult::FAIL:
      ENVOY_LOG(debug, "Header {} may not be removed. Returning error", remove_header);
      rejected_mutations.inc();
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid attempt to remove ", remove_header.get()));
    }
  }

  bool append_mode_for_append_action;
  Filters::Common::MutationRules::CheckOperation check_op_for_append_action;
  Filters::Common::MutationRules::CheckResult checkResult_for_append_action;
  auto is_duplicate = false;

  for (const auto& sh : mutation.set_headers()) {
    if (!sh.has_header()) {
      continue;
    }

    // Only one of value or raw_value in the HeaderValue message should be set.
    if (!sh.header().value().empty() && !sh.header().raw_value().empty()) {
      ENVOY_LOG(debug, "Only one of value or raw_value in the HeaderValue message should be set, "
                       "may not be append.");
      rejected_mutations.inc();
      return absl::InvalidArgumentError(
          "Only one of value or raw_value in the HeaderValue message should be set.");
    }

    absl::string_view header_value;
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.send_header_raw_value")) {
      header_value = sh.header().raw_value();
    } else {
      header_value = sh.header().value();
    }
    if (!Http::HeaderUtility::headerNameIsValid(sh.header().key()) ||
        !Http::HeaderUtility::headerValueIsValid(header_value)) {
      ENVOY_LOG(debug,
                "set_headers contain invalid character in key or value, may not be appended.");
      rejected_mutations.inc();
      return absl::InvalidArgumentError("Invalid character in set_headers mutation.");
    }
    const LowerCaseString header_name(sh.header().key());

    ENVOY_LOG(error, "append_action value: {}", sh.append_action());
    ENVOY_LOG(error, "header name: {}", header_name);
    ENVOY_LOG(error, "header value: {}, header raw value: {}", sh.header().value(),
              sh.header().raw_value());

    if (Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.header_value_option_change_action")) {
      switch (sh.append_action()) {
      case HeaderValueOption::APPEND_IF_EXISTS_OR_ADD:
        ENVOY_LOG(error, "Inside append action APPEND_IF_EXISTS_OR_ADD {} ",
                  HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);
        // Check if the header already exists with the same name and value.
        if (!headers.get(header_name).empty()) {
          is_duplicate = false;
          Http::HeaderMap::GetResult result = headers.get(header_name);
          for (size_t i = 0; i < result.size(); ++i) {
            const Http::HeaderEntry* entry = result[i];
            const absl::string_view& existing_value = entry->value().getStringView();

            // Compare the existing value with your desired header_value
            if (existing_value == header_value) {
              is_duplicate = true;
              break;
            }
          }
        }
        if (!is_duplicate) {
          append_mode_for_append_action = true;
          check_op_for_append_action =
              (!headers.get(header_name).empty()) ? CheckOperation::APPEND : CheckOperation::SET;
          checkResult_for_append_action = handleCheckResult(
              headers, replacing_message, checker, rejected_mutations, check_op_for_append_action,
              header_name, header_value, append_mode_for_append_action);
          if (checkResult_for_append_action == CheckResult::FAIL) {
            return absl::InvalidArgumentError(absl::StrCat(
                "Invalid attempt to modify ", static_cast<absl::string_view>(header_name)));
          }
        }
        break;
      case HeaderValueOption::ADD_IF_ABSENT:
        ENVOY_LOG(error, "Inside append action ADD_IF_ABSENT {}", HeaderValueOption::ADD_IF_ABSENT);
        if (headers.get(header_name).empty()) {
          append_mode_for_append_action = true;
          check_op_for_append_action = CheckOperation::SET;
          checkResult_for_append_action = handleCheckResult(
              headers, replacing_message, checker, rejected_mutations, check_op_for_append_action,
              header_name, header_value, append_mode_for_append_action);
          if (checkResult_for_append_action == CheckResult::FAIL) {
            return absl::InvalidArgumentError(absl::StrCat(
                "Invalid attempt to modify ", static_cast<absl::string_view>(header_name)));
          }
        }
        break;
      case HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD:
        ENVOY_LOG(error, "Inside append action OVERWRITE_IF_EXISTS_OR_ADD {}",
                  HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
        append_mode_for_append_action = false;
        check_op_for_append_action = CheckOperation::SET;
        checkResult_for_append_action = handleCheckResult(
            headers, replacing_message, checker, rejected_mutations, check_op_for_append_action,
            header_name, header_value, append_mode_for_append_action);
        if (checkResult_for_append_action == CheckResult::FAIL) {
          return absl::InvalidArgumentError(absl::StrCat(
              "Invalid attempt to modify ", static_cast<absl::string_view>(header_name)));
        }
        break;
      case HeaderValueOption::OVERWRITE_IF_EXISTS:
        ENVOY_LOG(error, "Inside append action OVERWRITE_IF_EXISTS{}",
                  HeaderValueOption::OVERWRITE_IF_EXISTS);
        if (!headers.get(header_name).empty()) {
          append_mode_for_append_action = false;
          check_op_for_append_action = CheckOperation::SET;
          checkResult_for_append_action = handleCheckResult(
              headers, replacing_message, checker, rejected_mutations, check_op_for_append_action,
              header_name, header_value, append_mode_for_append_action);
          if (checkResult_for_append_action == CheckResult::FAIL) {
            return absl::InvalidArgumentError(absl::StrCat(
                "Invalid attempt to modify ", static_cast<absl::string_view>(header_name)));
          }
        }
        break;
      default:
        // Handle unknown/invalid append_action value
        return absl::InvalidArgumentError(absl::StrCat(
            "Invalid append_action value ", static_cast<absl::string_view>(header_name)));
      }
    } else {
      const bool append_mode = PROTOBUF_GET_WRAPPED_OR_DEFAULT(sh, append, false);
      const auto check_op = (append_mode && !headers.get(header_name).empty())
                                ? CheckOperation::APPEND
                                : CheckOperation::SET;
      auto checkResult = handleCheckResult(headers, replacing_message, checker, rejected_mutations,
                                           check_op, header_name, header_value, append_mode);
      if (checkResult == CheckResult::FAIL) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Invalid attempt to modify ", static_cast<absl::string_view>(header_name)));
      }
    }
  }

  // After header mutation, check the ending headers are not exceeding the HCM limit.
  return headerMutationResultCheck(headers, rejected_mutations);
}

// Define a common function to handle the check result logic
Filters::Common::MutationRules::CheckResult MutationUtils::handleCheckResult(
    Http::HeaderMap& headers, bool replacing_message,
    const Filters::Common::MutationRules::Checker& checker, Stats::Counter& rejected_mutations,
    Filters::Common::MutationRules::CheckOperation check_op, Http::LowerCaseString header_name,
    absl::string_view header_value, bool append_mode) {
  auto check_result = checker.check(check_op, header_name, header_value);
  if (replacing_message && header_name == Http::Headers::get().Method) {
    // Special handling to allow changing ":method" when the
    // CONTINUE_AND_REPLACE option is selected, to stay compatible.
    check_result = CheckResult::OK;
  }

  switch (check_result) {
  case CheckResult::OK:
    ENVOY_LOG(error, "Setting header {} append = {}", header_name, append_mode);
    if (append_mode) {
      headers.addCopy(header_name, header_value);
    } else {
      headers.setCopy(header_name, header_value);
    }
    break;
  case CheckResult::IGNORE:
    ENVOY_LOG(debug, "Header {} may not be modified per rules", header_name);
    rejected_mutations.inc();
    break;
  case CheckResult::FAIL:
    ENVOY_LOG(debug, "Header {} may not be modified. Returning error", header_name);
    rejected_mutations.inc();
    // You can add additional handling for the FAIL case here if needed.
    break;
  }

  return check_result;
}

void MutationUtils::applyBodyMutations(const BodyMutation& mutation, Buffer::Instance& buffer) {
  switch (mutation.mutation_case()) {
  case BodyMutation::MutationCase::kClearBody:
    if (mutation.clear_body()) {
      ENVOY_LOG(trace, "Clearing HTTP body");
      buffer.drain(buffer.length());
    }
    break;
  case BodyMutation::MutationCase::kBody:
    ENVOY_LOG(trace, "Replacing body of {} bytes with new body of {} bytes", buffer.length(),
              mutation.body().size());
    buffer.drain(buffer.length());
    buffer.add(mutation.body());
    break;
  default:
    // Nothing to do on default
    break;
  }
}

bool MutationUtils::isValidHttpStatus(int code) { return (code >= 200); }

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
