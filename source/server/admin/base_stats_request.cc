#include "base_stats_request.h"
#include "source/server/admin/base_stats_request.h"

#include <iostream>
#include <string>
#include <vector>

#include "source/server/admin/prometheus_stats.h"

#include "stats_params.h"

#ifdef ENVOY_ADMIN_HTML
#include "source/server/admin/stats_html_render.h"
#endif

namespace Envoy {
namespace Server {

template <class TextReadoutType, class CounterType, class GaugeType, class HistogramType>
StatsRequestBase<TextReadoutType, CounterType, GaugeType, HistogramType>::StatsRequestBase(
    Stats::Store& stats, const StatsParams& params, UrlHandlerFn url_handler_fn)
    : stats_(stats), params_(params), url_handler_fn_(url_handler_fn) {}

template <class TextReadoutType, class CounterType, class GaugeType, class HistogramType>
Http::Code StatsRequestBase<TextReadoutType, CounterType, GaugeType, HistogramType>::start(
    Http::ResponseHeaderMap& response_headers) {
  switch (params_.format_) {
  case StatsFormat::Json:
    render_ = std::make_unique<StatsJsonRender>(response_headers, response_, params_);
    break;
  case StatsFormat::Text:
    render_ = std::make_unique<StatsTextRender>(params_);
    break;
#ifdef ENVOY_ADMIN_HTML
  case StatsFormat::Html: {
    auto html_render = std::make_unique<StatsHtmlRender>(response_headers, response_, params_);
    html_render->setSubmitOnChange(true);
    html_render->tableBegin(response_);
    html_render->urlHandler(response_, url_handler_fn_(), params_.query_);
    html_render->tableEnd(response_);
    html_render->startPre(response_);
    render_.reset(html_render.release());
    break;
  }
#endif
  case StatsFormat::Prometheus:
    render_ = std::make_unique<PrometheusStatsRender>();
    break;
  }

  // Populate the top-level scopes and the stats underneath any scopes with an empty name.
  // We will have to de-dup, but we can do that after sorting.
  //
  // First capture all the scopes and hold onto them with a SharedPtr so they
  // can't be deleted after the initial iteration.
  stats_.forEachScope(
      [this](size_t s) { scopes_.reserve(s); },
      [this](const Stats::Scope& scope) { scopes_.emplace_back(scope.getConstShared()); });

  startPhase();
  return Http::Code::OK;
}

template <class TextReadoutType, class CounterType, class GaugeType, class HistogramType>
bool StatsRequestBase<TextReadoutType, CounterType, GaugeType, HistogramType>::nextChunk(
    Buffer::Instance& response) {
  if (response_.length() > 0) {
    ASSERT(response.length() == 0);
    response.move(response_);
    ASSERT(response_.length() == 0);
  }

  // nextChunk's contract is to add up to chunk_size_ additional bytes. The
  // caller is not required to drain the bytes after each call to nextChunk.
  const uint64_t starting_response_length = response.length();
  while (response.length() - starting_response_length < chunk_size_) {
    while (stat_map_.empty()) {
      if (phase_stat_count_ == 0) {
        render_->noStats(response, phases_.get(phase_).phase_label);
      } else {
        phase_stat_count_ = 0;
      }
      if (params_.type_ != StatsType::All) {
        render_->finalize(response);
        return false;
      }

      // check if we have gone through all phases
      if (phase_ == phases_.size() - 1) {
        render_->finalize(response);
        return false;
      } else {
        phase_++;
        startPhase();
      }
    }

    auto iter = stat_map_.begin();
    StatOrScopes variant = std::move(iter->second);
    StatOrScopesIndex index = static_cast<StatOrScopesIndex>(variant.index());

    switch (index) {
    case StatOrScopesIndex::Scopes:
      // Erase the current element before adding new ones, as absl::btree_map
      // does not have stable iterators. When we hit leaf stats we will erase
      // second, so that we can use the name held as a map key, and don't need
      // to re-serialize the name from the symbol table.
      stat_map_.erase(iter);
      populateStatsForCurrentPhase(absl::get<ScopeVec>(variant));
      break;
    case StatOrScopesIndex::TextReadout:
      handleTextReadout(variant);
      stat_map_.erase(iter);
      break;
    case StatOrScopesIndex::Counter:
      handleCounter(variant);
      stat_map_.erase(iter);
      break;
    case StatOrScopesIndex::Gauge:
      handleGauge(variant);
      stat_map_.erase(iter);
      break;
    case StatOrScopesIndex::Histogram:
      handleHistogram(variant);
      stat_map_.erase(iter);
      break;
    }
  }
  return true;
}

template <class TextReadoutType, class CounterType, class GaugeType, class HistogramType>
void StatsRequestBase<TextReadoutType, CounterType, GaugeType, HistogramType>::startPhase() {
  ASSERT(stat_map_.empty());

  // Insert all the scopes in the alphabetically ordered map. As we iterate
  // through the map we'll erase the scopes and replace them with the stats held
  // in the scopes.
  for (const Stats::ConstScopeSharedPtr& scope : scopes_) {
    StatOrScopes& variant = stat_map_[stats_.symbolTable().toString(scope->prefix())];
    if (variant.index() == absl::variant_npos) {
      variant = ScopeVec();
    }
    absl::get<ScopeVec>(variant).emplace_back(scope);
  }
}

template <class TextReadoutType, class CounterType, class GaugeType, class HistogramType>
void StatsRequestBase<TextReadoutType, CounterType, GaugeType,
                      HistogramType>::populateStatsForCurrentPhase(const ScopeVec& scope_vec) {
  Phase current_phase = phases_.get(phase_);
  switch (current_phase.phase) {
  case PhaseName::TextReadouts:
    populateStatsFromScopes<Stats::TextReadout>(scope_vec);
    break;
  case PhaseName::CountersAndGauges:
    if (params_.type_ != StatsType::Gauges) {
      populateStatsFromScopes<Stats::Counter>(scope_vec);
    }
    if (params_.type_ != StatsType::Counters) {
      populateStatsFromScopes<Stats::Gauge>(scope_vec);
    }
    break;
  case PhaseName::Counters:
    populateStatsFromScopes<Stats::Counter>(scope_vec);
    break;
  case PhaseName::Gauges:
    populateStatsFromScopes<Stats::Gauge>(scope_vec);
    break;
  case PhaseName::Histograms:
    populateStatsFromScopes<Stats::Histogram>(scope_vec);
    break;
  }
}

} // namespace Server
} // namespace Envoy
