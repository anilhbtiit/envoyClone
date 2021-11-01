#include "source/common/stats/tag_utility.h"

#include <regex>

#include "source/common/config/well_known_names.h"
#include "source/common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {
namespace TagUtility {

TagStatNameJoiner::TagStatNameJoiner(StatName prefix, StatName stat_name,
                                     StatNameTagVectorOptConstRef stat_name_tags,
                                     SymbolTable& symbol_table) {
  prefix_storage_ = symbol_table.join({prefix, stat_name});
  tag_extracted_name_ = StatName(prefix_storage_.get());

  if (stat_name_tags) {
    full_name_storage_ =
        joinNameAndTags(StatName(prefix_storage_.get()), *stat_name_tags, symbol_table);
    name_with_tags_ = StatName(full_name_storage_.get());
  } else {
    name_with_tags_ = StatName(prefix_storage_.get());
  }
}

TagStatNameJoiner::TagStatNameJoiner(StatName stat_name,
                                     StatNameTagVectorOptConstRef stat_name_tags,
                                     SymbolTable& symbol_table) {
  tag_extracted_name_ = stat_name;

  if (stat_name_tags) {
    full_name_storage_ = joinNameAndTags(stat_name, *stat_name_tags, symbol_table);
    name_with_tags_ = StatName(full_name_storage_.get());
  } else {
    name_with_tags_ = stat_name;
  }
}

SymbolTable::StoragePtr TagStatNameJoiner::joinNameAndTags(StatName name,
                                                           const StatNameTagVector& tags,
                                                           SymbolTable& symbol_table) {
  StatNameVec stat_names;
  stat_names.reserve(1 + 2 * tags.size());
  stat_names.emplace_back(name);

  for (const auto& tag : tags) {
    stat_names.emplace_back(tag.first);
    stat_names.emplace_back(tag.second);
  }

  return symbol_table.join(stat_names);
}

bool isTagValueValid(absl::string_view name) {
  std::regex regex{Config::NAME_REGEX, std::regex::optimize};
  int cntr = 0;
  for (auto i = std::regex_iterator<absl::string_view::iterator>(name.begin(), name.end(), regex);
       i != std::regex_iterator<absl::string_view::iterator>(); ++i) {
    cntr++;
    if (cntr > 1) {
      return false;
    }
  }
  return true;
}

bool isTagNameValid(absl::string_view value) {
  for (const auto& token : value) {
    if (!absl::ascii_isalnum(token)) {
      return false;
    }
  }
  return true;
}

} // namespace TagUtility
} // namespace Stats
} // namespace Envoy
