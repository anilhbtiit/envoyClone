#include "source/server/admin/stats_handler.h"

#include <functional>
#include <vector>

#include "envoy/admin/v3/mutex_stats.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/html/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/server/admin/prometheus_stats.h"
#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

const uint64_t RecentLookupsCapacity = 100;

StatsHandler::StatsHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code StatsHandler::handlerResetCounters(absl::string_view, Http::ResponseHeaderMap&,
                                              Buffer::Instance& response, AdminStream&) {
  for (const Stats::CounterSharedPtr& counter : server_.stats().counters()) {
    counter->reset();
  }
  server_.stats().symbolTable().clearRecentLookups();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookups(absl::string_view, Http::ResponseHeaderMap&,
                                                   Buffer::Instance& response, AdminStream&) {
  Stats::SymbolTable& symbol_table = server_.stats().symbolTable();
  std::string table;
  const uint64_t total =
      symbol_table.getRecentLookups([&table](absl::string_view name, uint64_t count) {
        table += fmt::format("{:8d} {}\n", count, name);
      });
  if (table.empty() && symbol_table.recentLookupCapacity() == 0) {
    table = "Lookup tracking is not enabled. Use /stats/recentlookups/enable to enable.\n";
  } else {
    response.add("   Count Lookup\n");
  }
  response.add(absl::StrCat(table, "\ntotal: ", total, "\n"));
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookupsClear(absl::string_view, Http::ResponseHeaderMap&,
                                                        Buffer::Instance& response, AdminStream&) {
  server_.stats().symbolTable().clearRecentLookups();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookupsDisable(absl::string_view,
                                                          Http::ResponseHeaderMap&,
                                                          Buffer::Instance& response,
                                                          AdminStream&) {
  server_.stats().symbolTable().setRecentLookupCapacity(0);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookupsEnable(absl::string_view,
                                                         Http::ResponseHeaderMap&,
                                                         Buffer::Instance& response, AdminStream&) {
  server_.stats().symbolTable().setRecentLookupCapacity(RecentLookupsCapacity);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::Params::parse(absl::string_view url, Buffer::Instance& response) {
  const Http::Utility::QueryParams params = Http::Utility::parseAndDecodeQueryString(url);
  used_only_ = params.find("usedonly") != params.end();
  pretty_ = params.find("pretty") != params.end();
  text_readouts_ = params.find("text_readouts") != params.end();
  if (!Utility::filterParam(params, response, filter_)) {
    return Http::Code::BadRequest;
  }

  const absl::optional<std::string> format_value = Utility::formatParam(params);
  if (format_value.has_value()) {
    if (format_value.value() == "prometheus") {
      format_ = Format::Prometheus;
    } else if (format_value.value() == "json") {
      format_ = Format::Json;
    } else if (format_value.value() == "text") {
      format_ = Format::Text;
    } else {
      response.add("usage: /stats?format=json  or /stats?format=prometheus \n\n");
      return Http::Code::NotFound;
    }
  }

  Http::Utility::QueryParams::const_iterator scope_iter = params.find("scope");
  if (scope_iter != params.end()) {
    scope_ = scope_iter->second;
  }

  Http::Utility::QueryParams::const_iterator pagesize_iter = params.find("pagesize");
  if (pagesize_iter != params.end()) {
    // We don't accept arbitrary page sizes as they might be dangerous.
    if (pagesize_iter->second == "25") {
      page_size_ = 25;
    } else if (pagesize_iter->second == "100") {
      page_size_ = 100;
    } else if (pagesize_iter->second == "1000") {
      page_size_ = 1000;
    } else {
      response.add("pagesize must be 25, 100, or 1000");
      return Http::Code::NotFound;
    }
  }

  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStats(absl::string_view url,
                                      Http::ResponseHeaderMap& response_headers,
                                      Buffer::Instance& response, AdminStream&) {
  Params params;
  Http::Code code = params.parse(url, response);
  if (code != Http::Code::OK) {
    return code;
  }
  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }
  Stats::Store& store = server_.stats();
  if (params.format_ == Format::Prometheus) {
    const std::vector<Stats::TextReadoutSharedPtr>& text_readouts_vec =
        params.text_readouts_ ? store.textReadouts() : std::vector<Stats::TextReadoutSharedPtr>();
    PrometheusStatsFormatter::statsAsPrometheus(
        store.counters(), store.gauges(), store.histograms(), text_readouts_vec, response,
        params.used_only_, params.filter_, server_.api().customStatNamespaces());
    return Http::Code::OK;
  }

  return stats(params, server_.stats(), response_headers, response);
}

Http::Code StatsHandler::handlerStatsJson(absl::string_view url,
                                          Http::ResponseHeaderMap& response_headers,
                                          Buffer::Instance& response, AdminStream&) {
  Params params;
  Http::Code code = params.parse(url, response);
  if (code != Http::Code::OK) {
    return code;
  }
  params.format_ = Format::Json;
  return stats(params, server_.stats(), response_headers, response);
}

class StatsHandler::Render {
 public:
  virtual ~Render() = default;
  virtual void counter(Stats::Counter&) PURE;
  virtual void gauge(Stats::Gauge&) PURE;
  virtual void textReadout(Stats::TextReadout&) PURE;
  virtual void histogram(Stats::Histogram&) PURE;
};

class StatsHandler::TextRender : public StatsHandler::Render {
 public:
  explicit TextRender(Buffer::Instance& response) : response_(response) {}
  void counter(Stats::Counter& counter) override {
    response_.add(absl::StrCat(counter.name(), ": ", counter.value(), "\n"));
  }
  void gauge(Stats::Gauge& gauge) override {
    response_.add(absl::StrCat(gauge.name(), ": ", gauge.value(), "\n"));
  }
  void textReadout(Stats::TextReadout& text_readout) override {
    response_.add(absl::StrCat(text_readout.name(), ": \"",
                               Html::Utility::sanitize(text_readout.value()), "\"\n"));
  }
  void histogram(Stats::Histogram& histogram) override {
    Stats::ParentHistogram* phist = dynamic_cast<Stats::ParentHistogram*>(&histogram);
    if (phist != nullptr) {
      response_.add(absl::StrCat(phist->name(), ": ", phist->quantileSummary(), "\n"));
    }
  }

 private:
  Buffer::Instance& response_;
};

class StatsHandler::JsonRender : public StatsHandler::Render {
 public:
  JsonRender(Buffer::Instance& response, const Params& params)
      : params_(params),
        response_(response) {}
  virtual ~JsonRender() { render(); }

  void counter(Stats::Counter& counter) override {
    add(counter, ValueUtil::numberValue(counter.value()));
  }
  void gauge(Stats::Gauge& gauge) override { add(gauge, ValueUtil::numberValue(gauge.value())); }
  void textReadout(Stats::TextReadout& text_readout) override {
    add(text_readout, ValueUtil::stringValue(text_readout.value()));
  }
  void histogram(Stats::Histogram& histogram) override {
    Stats::ParentHistogram* phist = dynamic_cast<Stats::ParentHistogram*>(&histogram);
    if (phist != nullptr) {
      if (!found_used_histogram_) {
        auto* histograms_obj_fields = histograms_obj_.mutable_fields();

        // It is not possible for the supported quantiles to differ across histograms, so it is ok
        // to send them once.
        Stats::HistogramStatisticsImpl empty_statistics;
        std::vector<ProtobufWkt::Value> supported_quantile_array;
        for (double quantile : empty_statistics.supportedQuantiles()) {
          supported_quantile_array.push_back(ValueUtil::numberValue(quantile * 100));
        }
        (*histograms_obj_fields)["supported_quantiles"] =
            ValueUtil::listValue(supported_quantile_array);
        found_used_histogram_ = true;
      }

      ProtobufWkt::Struct computed_quantile;
      auto* computed_quantile_fields = computed_quantile.mutable_fields();
      (*computed_quantile_fields)["name"] = ValueUtil::stringValue(histogram.name());

      std::vector<ProtobufWkt::Value> computed_quantile_value_array;
      for (size_t i = 0; i < phist->intervalStatistics().supportedQuantiles().size(); ++i) {
        ProtobufWkt::Struct computed_quantile_value;
        auto* computed_quantile_value_fields = computed_quantile_value.mutable_fields();
        const auto& interval = phist->intervalStatistics().computedQuantiles()[i];
        const auto& cumulative = phist->cumulativeStatistics().computedQuantiles()[i];
        (*computed_quantile_value_fields)["interval"] =
            std::isnan(interval) ? ValueUtil::nullValue() : ValueUtil::numberValue(interval);
        (*computed_quantile_value_fields)["cumulative"] =
            std::isnan(cumulative) ? ValueUtil::nullValue() : ValueUtil::numberValue(cumulative);

        computed_quantile_value_array.push_back(ValueUtil::structValue(computed_quantile_value));
      }
      (*computed_quantile_fields)["values"] = ValueUtil::listValue(computed_quantile_value_array);
      computed_quantile_array_.push_back(ValueUtil::structValue(computed_quantile));
    }
  }

 private:
  void render() {
    if (found_used_histogram_) {
      auto* histograms_obj_fields = histograms_obj_.mutable_fields();
      (*histograms_obj_fields)["computed_quantiles"] =
          ValueUtil::listValue(computed_quantile_array_);
      auto* histograms_obj_container_fields = histograms_obj_container_.mutable_fields();
      (*histograms_obj_container_fields)["histograms"] = ValueUtil::structValue(histograms_obj_);
      stats_array_.push_back(ValueUtil::structValue(histograms_obj_container_));
    }

    auto* document_fields = document_.mutable_fields();
    (*document_fields)["stats"] = ValueUtil::listValue(stats_array_);
    response_.add(MessageUtil::getJsonStringFromMessageOrDie(document_, params_.pretty_, true));
  }

  template<class StatType, class Value> void add(StatType& stat, const Value& value) {
    ProtobufWkt::Struct stat_obj;
    auto* stat_obj_fields = stat_obj.mutable_fields();
    (*stat_obj_fields)["name"] = ValueUtil::stringValue(stat.name());
    (*stat_obj_fields)["value"] = value;
    stats_array_.push_back(ValueUtil::structValue(stat_obj));
  }

  const StatsHandler::Params& params_;
  ProtobufWkt::Struct document_;
  std::vector<ProtobufWkt::Value> stats_array_;
  ProtobufWkt::Struct histograms_obj_;
  ProtobufWkt::Struct histograms_obj_container_;
  std::vector<ProtobufWkt::Value> computed_quantile_array_;
  bool found_used_histogram_{false};
  Buffer::Instance& response_;
};

class StatsHandler::Context {
 public:
  Context(const Params& params, Render& render) : params_(params), render_(render) {}
  absl::string_view start() { return params_.start_; }

  // Quick check to see if we're at the end of the page. If we are, we record the
  // name of the stat we are going to reject.
  template <class StatType> bool checkEndOfPage(const StatType& metric) {
    if (params_.page_size_.has_value()) {
      ASSERT(num_ <= params_.page_size_.value());
      if (num_ == params_.page_size_.value()) {
        return true;
      } else if (next_start_.empty()) {
        next_start_ = metric.name();
      }
    }
    return false;
  }

  /*
  template <class StatType> bool checkMetricAndEndOfPage(const StatType& metric) {
    // Do a relatively fast check to see whether we have hit the end of the page,
    //

    ++num_;
    if (params_.page_size_.has_value()) {
      if (num_ == params_.page_size_.value()) {
        next_start_ = "";//metric->name();
        return true;
      } else if (num_ > params_.page_size_.value()) {
        return true;
      }
    }
    return false;
  }
  */

  template <class StatType> bool showMetric(const StatType& metric) {
    if (params_.shouldShowMetric(metric)) {
      ++num_;
      return true;
    }
    return false;
  }

  Render& render() { return render_; }

  int64_t num_{-1};
  const Params& params_;
  Render& render_;
  std::string next_start_;
};

Http::Code StatsHandler::stats(const Params& params, Stats::Store& stats,
                               Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response) {
  std::unique_ptr<Render> render;
  if (params.format_ == Format::Json) {
    render = std::make_unique<JsonRender>(response, params);
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  } else {
    render = std::make_unique<TextRender>(response);
  }

  Context context(params, *render);

  auto tfn = [&context](Stats::TextReadout& text_readout) -> bool {
    if (context.checkEndOfPage(text_readout)) {
      return false;
    }
    if (context.showMetric(text_readout)) {
      context.render().textReadout(text_readout);
    }
    return true;
  };
  stats.textReadoutPage(tfn, context.start());

  auto cfn = [&context](Stats::Counter& counter) -> bool {
    if (context.checkEndOfPage(counter)) {
      return false;
    }
    if (context.showMetric(counter)) {
      context.render().counter(counter);
    }
    return true;
  };
  stats.counterPage(cfn, context.start());

  auto gfn = [&context](Stats::Gauge& gauge) -> bool {
    if (context.checkEndOfPage(gauge)) {
      return false;
    }
    if (context.showMetric(gauge)) {
      ASSERT(gauge.importMode() != Stats::Gauge::ImportMode::Uninitialized);
      context.render().gauge(gauge);
    }
    return true;
  };
  stats.gaugePage(gfn, context.start());

  auto hfn = [&context](Stats::Histogram& histogram) -> bool {
    if (context.checkEndOfPage(histogram)) {
      return false;
    }
    if (context.showMetric(histogram)) {
      context.render().histogram(histogram);
    }
    return true;
  };
  stats.histogramPage(hfn, context.start());

#if 0
  if (params.scope_.has_value()) {
    Stats::StatNameManagedStorage scope_name(params.scope_.value(), stats.symbolTable());
    stats.forEachScope([](size_t) {},
                       [&scope_name, &append_stats_from_scope, &context](
                           const Stats::Scope& scope) -> bool {
                         if (context.checkEndOfPage(scope)) {
                           return false;
                         }
                         if (scope.prefix() == scope_name.statName()) {
                           append_stats_from_scope(scope);
                         }
                         return true;
                       });
  }
#endif


  // Display plain stats if format query param is not there.
  //statsAsText(counters_and_gauges, text_readouts, histograms, response);
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsScopes(absl::string_view,
                                            Http::ResponseHeaderMap& response_headers,
                                            Buffer::Instance& response, AdminStream&) {
  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }

  std::string preamble = R"(<html>
  <head>
    <script>
      function visitScope(scope) {
        var params = "";
        if (document.getElementById("used").checked) {
          params += "&usedonly";
        }
        var filter = document.getElementById("filter").value;
        if (filter && filter.length > 0) {
          params += "&filter=" + filter;
        }
        location.href = "/stats?scope=" + scope + params;
      }
    </script>
  </head>
  <body>
    <label for="used">Used Only</label><input type="checkbox" id="used"><br>
    <label for="filter">Filter (regex)</label><input type="text" id="filter"><br>
)";

  Stats::StatNameHashSet prefixes;
  server_.stats().forEachScope([](size_t) {},
                               [&prefixes](const Stats::Scope& scope) -> bool {
                                 prefixes.insert(scope.prefix());
                                 return true;
                               });
  std::vector<std::string> lines, names;
  names.reserve(prefixes.size());
  lines.reserve(prefixes.size() + 2);
  lines.push_back(preamble);
  for (Stats::StatName prefix : prefixes) {
    names.emplace_back(server_.stats().symbolTable().toString(prefix));
  }
  std::sort(names.begin(), names.end());
  for (const std::string& name : names) {
    lines.push_back(
        absl::StrCat("    <a href='javascript:visitScope(\"", name, "\")'>", name, "</a><br>\n"));
  }
  lines.push_back("  </body>\n</html>\n");
  response.add(absl::StrJoin(lines, ""));
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Html);
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsPrometheus(absl::string_view path_and_query,
                                                Http::ResponseHeaderMap&,
                                                Buffer::Instance& response, AdminStream&) {
  Params params;
  Http::Code code = params.parse(path_and_query, response);
  if (code != Http::Code::OK) {
    return code;
  }
  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }
  Stats::Store& stats = server_.stats();
  const std::vector<Stats::TextReadoutSharedPtr>& text_readouts_vec =
      params.text_readouts_ ? stats.textReadouts() : std::vector<Stats::TextReadoutSharedPtr>();
  PrometheusStatsFormatter::statsAsPrometheus(stats.counters(), stats.gauges(), stats.histograms(),
                                              text_readouts_vec, response, params.used_only_,
                                              params.filter_, server_.api().customStatNamespaces());
  return Http::Code::OK;
}

// TODO(ambuc) Export this as a server (?) stat for monitoring.
Http::Code StatsHandler::handlerContention(absl::string_view,
                                           Http::ResponseHeaderMap& response_headers,
                                           Buffer::Instance& response, AdminStream&) {

  if (server_.options().mutexTracingEnabled() && server_.mutexTracer() != nullptr) {
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);

    envoy::admin::v3::MutexStats mutex_stats;
    mutex_stats.set_num_contentions(server_.mutexTracer()->numContentions());
    mutex_stats.set_current_wait_cycles(server_.mutexTracer()->currentWaitCycles());
    mutex_stats.set_lifetime_wait_cycles(server_.mutexTracer()->lifetimeWaitCycles());
    response.add(MessageUtil::getJsonStringFromMessageOrError(mutex_stats, true, true));
  } else {
    response.add("Mutex contention tracing is not enabled. To enable, run Envoy with flag "
                 "--enable-mutex-tracing.");
  }
  return Http::Code::OK;
}

void StatsHandler::statsAsText(const std::map<std::string, uint64_t>& counters_and_gauges,
                               const std::map<std::string, std::string>& text_readouts,
                               const std::vector<Stats::HistogramSharedPtr>& histograms,
                               Buffer::Instance& response) {
  // Display plain stats if format query param is not there.
  for (const auto& text_readout : text_readouts) {
    response.add(fmt::format("{}: \"{}\"\n", text_readout.first,
                             Html::Utility::sanitize(text_readout.second)));
  }
  for (const auto& stat : counters_and_gauges) {
    response.add(fmt::format("{}: {}\n", stat.first, stat.second));
  }
  for (const auto& histogram : histograms) {
    Stats::ParentHistogram* phist = dynamic_cast<Stats::ParentHistogram*>(histogram.get());
    if (phist != nullptr) {
      response.add(fmt::format("{}: {}\n", phist->name(), phist->quantileSummary()));
    }
  }
}

std::string StatsHandler::statsAsJson(const std::map<std::string, uint64_t>& counters_and_gauges,
                                      const std::map<std::string, std::string>& text_readouts,
                                      const std::vector<Stats::HistogramSharedPtr>& all_histograms,
                                      const bool pretty_print) {

  ProtobufWkt::Struct document;
  std::vector<ProtobufWkt::Value> stats_array;
  for (const auto& text_readout : text_readouts) {
    ProtobufWkt::Struct stat_obj;
    auto* stat_obj_fields = stat_obj.mutable_fields();
    (*stat_obj_fields)["name"] = ValueUtil::stringValue(text_readout.first);
    (*stat_obj_fields)["value"] = ValueUtil::stringValue(text_readout.second);
    stats_array.push_back(ValueUtil::structValue(stat_obj));
  }
  for (const auto& stat : counters_and_gauges) {
    ProtobufWkt::Struct stat_obj;
    auto* stat_obj_fields = stat_obj.mutable_fields();
    (*stat_obj_fields)["name"] = ValueUtil::stringValue(stat.first);
    (*stat_obj_fields)["value"] = ValueUtil::numberValue(stat.second);
    stats_array.push_back(ValueUtil::structValue(stat_obj));
  }

  ProtobufWkt::Struct histograms_obj;
  auto* histograms_obj_fields = histograms_obj.mutable_fields();

  ProtobufWkt::Struct histograms_obj_container;
  auto* histograms_obj_container_fields = histograms_obj_container.mutable_fields();
  std::vector<ProtobufWkt::Value> computed_quantile_array;

  bool found_used_histogram = false;
  for (const Stats::HistogramSharedPtr& histogram : all_histograms) {
    Stats::ParentHistogram* phist = dynamic_cast<Stats::ParentHistogram*>(histogram.get());
    if (phist != nullptr) {
      if (!found_used_histogram) {
        // It is not possible for the supported quantiles to differ across histograms, so it is ok
        // to send them once.
        Stats::HistogramStatisticsImpl empty_statistics;
        std::vector<ProtobufWkt::Value> supported_quantile_array;
        for (double quantile : empty_statistics.supportedQuantiles()) {
          supported_quantile_array.push_back(ValueUtil::numberValue(quantile * 100));
        }
        (*histograms_obj_fields)["supported_quantiles"] =
            ValueUtil::listValue(supported_quantile_array);
        found_used_histogram = true;
      }

      ProtobufWkt::Struct computed_quantile;
      auto* computed_quantile_fields = computed_quantile.mutable_fields();
      (*computed_quantile_fields)["name"] = ValueUtil::stringValue(histogram->name());

      std::vector<ProtobufWkt::Value> computed_quantile_value_array;
      for (size_t i = 0; i < phist->intervalStatistics().supportedQuantiles().size(); ++i) {
        ProtobufWkt::Struct computed_quantile_value;
        auto* computed_quantile_value_fields = computed_quantile_value.mutable_fields();
        const auto& interval = phist->intervalStatistics().computedQuantiles()[i];
        const auto& cumulative = phist->cumulativeStatistics().computedQuantiles()[i];
        (*computed_quantile_value_fields)["interval"] =
            std::isnan(interval) ? ValueUtil::nullValue() : ValueUtil::numberValue(interval);
        (*computed_quantile_value_fields)["cumulative"] =
            std::isnan(cumulative) ? ValueUtil::nullValue() : ValueUtil::numberValue(cumulative);

        computed_quantile_value_array.push_back(ValueUtil::structValue(computed_quantile_value));
      }
      (*computed_quantile_fields)["values"] = ValueUtil::listValue(computed_quantile_value_array);
      computed_quantile_array.push_back(ValueUtil::structValue(computed_quantile));
    }
  }

  if (found_used_histogram) {
    (*histograms_obj_fields)["computed_quantiles"] = ValueUtil::listValue(computed_quantile_array);
    (*histograms_obj_container_fields)["histograms"] = ValueUtil::structValue(histograms_obj);
    stats_array.push_back(ValueUtil::structValue(histograms_obj_container));
  }

  auto* document_fields = document.mutable_fields();
  (*document_fields)["stats"] = ValueUtil::listValue(stats_array);

  ENVOY_LOG_MISC(error, "pretty_print={}", pretty_print);
  return MessageUtil::getJsonStringFromMessageOrDie(document, pretty_print, true);
}

} // namespace Server
} // namespace Envoy
