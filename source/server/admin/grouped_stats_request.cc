#include "source/server/admin/grouped_stats_request.h"

#include <string>
#include <vector>

#include "source/server/admin/stats_params.h"
#include "source/server/admin/stats_render.h"

namespace Envoy {
namespace Server {

// TODO(rulex123): this is currently used for Prometheus stats only, and
// contains some Prometheus-specific logic (e.g. text-readouts policy). We should
// remove any format-specific logic if we decide to have a grouped view for HTML or JSON stats.
GroupedStatsRequest::GroupedStatsRequest(Stats::Store& stats, const StatsParams& params,
                                         Stats::CustomStatNamespaces& custom_namespaces,
                                         UrlHandlerFn url_handler_fn)
    : StatsRequest(stats, params, url_handler_fn), custom_namespaces_(custom_namespaces),
      global_symbol_table_(stats.constSymbolTable()) {

  // the "type" query param is ignored for prometheus stats, so always start from
  // counters; also, skip the TextReadouts phase unless that stat type is explicitly
  // requested via query param
  if (params_.prometheus_text_readouts_) {
    phases_ = {Phase{PhaseName::Counters, "Counters"}, Phase{PhaseName::Gauges, "Gauges"},
               Phase{PhaseName::TextReadouts, "Text Readouts"},
               Phase{PhaseName::Histograms, "Histograms"}};
  } else {
    phases_ = {Phase{PhaseName::Counters, "Counters"}, Phase{PhaseName::Gauges, "Gauges"},
               Phase{PhaseName::Histograms, "Histograms"}};
  }
  phase_index_ = 0;
}

template <class StatType> Stats::IterateFn<StatType> GroupedStatsRequest::saveMatchingStat() {
  return [this](const Stats::RefcountPtr<StatType>& stat) -> bool {
    // check if unused
    if (params_.used_only_ && !stat->used()) {
      return true;
    }

    // check if filtered
    if (params_.filter_ != nullptr) {
      if (!std::regex_search(stat->name(), *params_.filter_)) {
        return true;
      }
    } else if (params_.re2_filter_ != nullptr &&
               !re2::RE2::PartialMatch(stat->name(), *params_.re2_filter_)) {
      return true;
    }

    // capture stat by either adding to a pre-existing variant or by creating a new variant
    std::string tag_extracted_name = global_symbol_table_.toString(stat->tagExtractedStatName());
    auto [iterator, inserted] = stat_map_.try_emplace(
        tag_extracted_name, std::vector<Stats::RefcountPtr<StatType>>({stat}));
    if (!inserted) {
      absl::get<std::vector<Stats::RefcountPtr<StatType>>>(iterator->second).emplace_back(stat);
    }
    return true;
  };
}

Stats::IterateFn<Stats::TextReadout> GroupedStatsRequest::saveMatchingStatForTextReadout() {
  return saveMatchingStat<Stats::TextReadout>();
}

Stats::IterateFn<Stats::Gauge> GroupedStatsRequest::saveMatchingStatForGauge() {
  return saveMatchingStat<Stats::Gauge>();
}

Stats::IterateFn<Stats::Counter> GroupedStatsRequest::saveMatchingStatForCounter() {
  return saveMatchingStat<Stats::Counter>();
}

Stats::IterateFn<Stats::Histogram> GroupedStatsRequest::saveMatchingStatForHistogram() {
  return saveMatchingStat<Stats::Histogram>();
}

template <class SharedStatType>
void GroupedStatsRequest::renderStat(const std::string& name, Buffer::Instance& response,
                                     const StatOrScopes& variant) {
  auto prefixed_tag_extracted_name = prefixedTagExtractedName<SharedStatType>(name);
  if (prefixed_tag_extracted_name.has_value()) {
    PrometheusStatsRender* const prometheus_render =
        dynamic_cast<PrometheusStatsRender*>(render_.get());
    ++phase_stat_count_;

    // sort group
    std::vector<SharedStatType> group = absl::get<std::vector<SharedStatType>>(variant);
    global_symbol_table_.sortByStatNames<Stats::RefcountPtr<Stats::Metric>>(
        group.begin(), group.end(),
        [](const Stats::RefcountPtr<Stats::Metric>& stat) -> Stats::StatName {
          return stat->statName();
        });

    // render group
    StatOrScopesIndex index = static_cast<StatOrScopesIndex>(variant.index());
    // This code renders stats of type counter, gauge or text-readout: text readout
    // stats are returned in gauge format, so "gauge" type is set intentionally.
    std::string type = (index == StatOrScopesIndex::Counter) ? "counter" : "gauge";

    response.add(fmt::format("# TYPE {0} {1}\n", prefixed_tag_extracted_name.value(), type));
    for (SharedStatType metric : group) {
      prometheus_render->generate(response, prefixed_tag_extracted_name.value(), *metric.get());
    }
  }
}

void GroupedStatsRequest::processTextReadout(const std::string& name, Buffer::Instance& response,
                                             const StatOrScopes& variant) {
  renderStat<Stats::TextReadoutSharedPtr>(name, response, variant);
}

void GroupedStatsRequest::processCounter(const std::string& name, Buffer::Instance& response,
                                         const StatOrScopes& variant) {
  renderStat<Stats::CounterSharedPtr>(name, response, variant);
}

void GroupedStatsRequest::processGauge(const std::string& name, Buffer::Instance& response,
                                       const StatOrScopes& variant) {
  renderStat<Stats::GaugeSharedPtr>(name, response, variant);
}

void GroupedStatsRequest::processHistogram(const std::string& name, Buffer::Instance& response,
                                           const StatOrScopes& variant) {
  auto histogram = absl::get<std::vector<Stats::HistogramSharedPtr>>(variant);
  auto prefixed_tag_extracted_name = prefixedTagExtractedName<Stats::HistogramSharedPtr>(name);

  if (prefixed_tag_extracted_name.has_value()) {
    // increment stats count
    phase_stat_count_++;

    // sort group
    global_symbol_table_.sortByStatNames<Stats::RefcountPtr<Stats::Metric>>(
        histogram.begin(), histogram.end(),
        [](const Stats::RefcountPtr<Stats::Metric>& stat) -> Stats::StatName {
          return stat->statName();
        });

    // render group
    response.add(fmt::format("# TYPE {0} {1}\n", prefixed_tag_extracted_name.value(), "histogram"));
    for (const auto& metric : histogram) {
      auto parent_histogram = dynamic_cast<Stats::ParentHistogram*>(metric.get());
      if (parent_histogram != nullptr) {
        render_->generate(response, prefixed_tag_extracted_name.value(), *parent_histogram);
      }
    }
  }
}

template <class SharedStatType>
absl::optional<std::string>
GroupedStatsRequest::prefixedTagExtractedName(const std::string& tag_extracted_name) {
  return Envoy::Server::PrometheusStatsRender::metricName(tag_extracted_name, custom_namespaces_);
}

} // namespace Server
} // namespace Envoy
